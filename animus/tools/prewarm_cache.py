#!/usr/bin/env python3
"""Prewarm Animus terrain cache inputs without implicit network access."""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
import sys
import time
import urllib.request
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass(frozen=True)
class TileCoord:
    z: int
    x: int
    y: int

    @property
    def key(self) -> str:
        return f"{self.z}/{self.x}/{self.y}"


def parse_bbox(value: str) -> tuple[float, float, float, float]:
    parts = [float(part) for part in value.split(",")]
    if len(parts) != 4:
        raise ValueError("--bbox must be west,south,east,north")
    west, south, east, north = parts
    if west >= east or south >= north:
        raise ValueError("--bbox must be ordered west<south/east<north")
    return west, south, east, north


def lon_to_tile_x(lon_deg: float, z: int) -> int:
    axis = 1 << z
    return max(0, min(axis - 1, int((lon_deg + 180.0) / 360.0 * axis)))


def lat_to_tile_y(lat_deg: float, z: int) -> int:
    import math

    axis = 1 << z
    lat_rad = math.radians(max(-85.0511287798066, min(85.0511287798066, lat_deg)))
    value = (1.0 - math.asinh(math.tan(lat_rad)) / math.pi) * 0.5 * axis
    return max(0, min(axis - 1, int(value)))


def tiles_for_bbox(
    bbox: tuple[float, float, float, float], min_z: int, max_z: int
) -> list[TileCoord]:
    west, south, east, north = bbox
    coords: list[TileCoord] = []
    for z in range(min_z, max_z + 1):
        x0 = lon_to_tile_x(west, z)
        x1 = lon_to_tile_x(east, z)
        y0 = lat_to_tile_y(north, z)
        y1 = lat_to_tile_y(south, z)
        coords.extend(TileCoord(z, x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))
    return coords


def cache_path(cache_root: Path, layer: str, coord: TileCoord, extension: str) -> Path:
    return cache_root / layer / str(coord.z) / str(coord.x) / f"{coord.y}.{extension}"


def prewarm_local_xyz(
    pack_root: Path, cache_root: Path, layers: list[str], coords: list[TileCoord]
) -> dict:
    copied = 0
    missing = 0
    for layer in layers:
        extension = "f32" if layer == "bathymetry" else "png"
        for coord in coords:
            source = pack_root / layer / str(coord.z) / str(coord.x) / f"{coord.y}.{extension}"
            if not source.exists():
                missing += 1
                continue
            target = cache_path(cache_root, layer, coord, extension)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read_bytes())
            copied += 1
    return {"kind": "local_xyz", "copied": copied, "missing": missing}


def prewarm_mbtiles(path: Path, cache_root: Path, layer: str, coords: list[TileCoord]) -> dict:
    hits = 0
    missing = 0
    with sqlite3.connect(path) as db:
        for coord in coords:
            tile_row = (1 << coord.z) - 1 - coord.y
            row = db.execute(
                "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                (coord.z, coord.x, tile_row),
            ).fetchone()
            if row is None:
                missing += 1
                continue
            target = cache_path(cache_root, layer, coord, "tile")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(row[0])
            hits += 1
    return {"kind": "mbtiles", "path": str(path), "hits": hits, "missing": missing}


def prewarm_remote(
    url_template: str, cache_root: Path, layer: str, coords: list[TileCoord], user_agent: str
) -> dict:
    fetched = 0
    failures = 0
    for coord in coords:
        url = (
            url_template.replace("{z}", str(coord.z))
            .replace("{x}", str(coord.x))
            .replace("{y}", str(coord.y))
        )
        request = urllib.request.Request(url, headers={"User-Agent": user_agent})
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                data = response.read()
        except Exception:
            failures += 1
            continue
        suffix = Path(url.split("?", 1)[0]).suffix.lower() or ".tile"
        target = cache_path(cache_root, layer, coord, suffix.lstrip("."))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        fetched += 1
    return {"kind": "remote_http", "fetched": fetched, "failures": failures}


def write_summary(summary: dict, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    digest = hashlib.sha1(json.dumps(summary, sort_keys=True).encode("utf-8")).hexdigest()[:8]
    path = output_dir / f"prewarm-{stamp}-{digest}.json"
    path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bbox", required=True, help="west,south,east,north degrees")
    parser.add_argument("--min-z", type=int, required=True)
    parser.add_argument("--max-z", type=int, required=True)
    parser.add_argument("--cache-root", type=Path, required=True)
    parser.add_argument("--pack-root", type=Path)
    parser.add_argument("--layers", default="imagery,elevation")
    parser.add_argument("--mbtiles", type=Path)
    parser.add_argument("--remote-url")
    parser.add_argument("--remote-user-agent", default="Animus/0.1")
    parser.add_argument("--summary-dir", type=Path, default=Path("artifacts/animus/prewarm"))
    args = parser.parse_args(argv)

    bbox = parse_bbox(args.bbox)
    if args.min_z > args.max_z:
        raise ValueError("--min-z must be <= --max-z")
    coords = tiles_for_bbox(bbox, args.min_z, args.max_z)
    layers = [layer for layer in args.layers.split(",") if layer]
    sources = []
    if args.pack_root is not None:
        sources.append(prewarm_local_xyz(args.pack_root, args.cache_root, layers, coords))
    if args.mbtiles is not None:
        sources.append(prewarm_mbtiles(args.mbtiles, args.cache_root, layers[0], coords))
    if args.remote_url:
        sources.append(
            prewarm_remote(
                args.remote_url, args.cache_root, layers[0], coords, args.remote_user_agent
            )
        )

    summary = {
        "schema": "animus.cache_prewarm.v1",
        "bbox": list(bbox),
        "tile_count": len(coords),
        "tiles": [asdict(coord) for coord in coords],
        "sources": sources,
    }
    path = write_summary(summary, args.summary_dir)
    print(
        json.dumps(
            {"summary": str(path), "tile_count": len(coords), "sources": sources}, sort_keys=True
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"prewarm_cache.py: {exc}", file=sys.stderr)
        raise SystemExit(2)

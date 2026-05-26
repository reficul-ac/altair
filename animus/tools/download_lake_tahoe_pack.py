#!/usr/bin/env python3
"""Download and prepare the local Lake Tahoe Phase D terrain pack."""

from __future__ import annotations

import argparse
import io
import json
import math
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from terrain_pack import (
    TileCoord,
    add_manifest_argument,
    load_manifest,
    manifest_tile_sets,
    required_coords,
    validate_pack,
)

TOOL_VERSION = "1"
PROVENANCE_SCHEMA = "animus.lake_tahoe_pack_provenance.v1"
TILE_SIZE = 256
WEB_MERCATOR_HALF_WORLD_M = 20037508.342789244
IMAGERY_TILE_URL_TEMPLATE = (
    "https://basemap.nationalmap.gov/arcgis/rest/services/"
    "USGSImageryOnly/MapServer/tile/{z}/{y}/{x}"
)
ELEVATION_EXPORT_URL = (
    "https://elevation.nationalmap.gov/arcgis/rest/services/"
    "3DEPElevation/ImageServer/exportImage"
)


@dataclass(frozen=True)
class TileWorkItem:
    coord: TileCoord
    imagery_url: str
    imagery_output: Path
    elevation_url: str
    elevation_output: Path


def imagery_tile_url(coord: TileCoord) -> str:
    return IMAGERY_TILE_URL_TEMPLATE.format(z=coord.z, y=coord.y, x=coord.x)


def tile_bounds_epsg3857(coord: TileCoord) -> tuple[float, float, float, float]:
    if coord.z < 0:
        raise ValueError("tile zoom must be non-negative")
    axis_count = 1 << coord.z
    if coord.x < 0 or coord.y < 0 or coord.x >= axis_count or coord.y >= axis_count:
        raise ValueError(f"tile coordinate is outside zoom {coord.z}: {coord.key}")

    world_size = WEB_MERCATOR_HALF_WORLD_M * 2.0
    tile_size_m = world_size / axis_count
    west = -WEB_MERCATOR_HALF_WORLD_M + coord.x * tile_size_m
    east = west + tile_size_m
    north = WEB_MERCATOR_HALF_WORLD_M - coord.y * tile_size_m
    south = north - tile_size_m
    return (west, south, east, north)


def elevation_export_url(coord: TileCoord, tile_size: int = TILE_SIZE) -> str:
    west, south, east, north = tile_bounds_epsg3857(coord)
    params = {
        "bbox": f"{west:.9f},{south:.9f},{east:.9f},{north:.9f}",
        "bboxSR": "3857",
        "imageSR": "3857",
        "size": f"{tile_size},{tile_size}",
        "format": "tiff",
        "pixelType": "F32",
        "interpolation": "RSP_BilinearInterpolation",
        "f": "image",
    }
    return f"{ELEVATION_EXPORT_URL}?{urlencode(params)}"


def terrain_rgb_triplet(meters: float) -> tuple[int, int, int]:
    if not math.isfinite(meters):
        raise ValueError("cannot encode non-finite elevation")
    encoded = int(round((meters + 10000.0) * 10.0))
    if encoded < 0 or encoded > 0xFFFFFF:
        raise ValueError(f"elevation is outside Terrain-RGB range: {meters}")
    return ((encoded >> 16) & 0xFF, (encoded >> 8) & 0xFF, encoded & 0xFF)


def terrain_rgb_height(red: int, green: int, blue: int) -> float:
    encoded = red * 256 * 256 + green * 256 + blue
    return -10000.0 + encoded * 0.1


def build_work_items(manifest: dict[str, Any], pack_root: Path) -> list[TileWorkItem]:
    return [
        TileWorkItem(
            coord=coord,
            imagery_url=imagery_tile_url(coord),
            imagery_output=pack_root / "imagery" / str(coord.z) / str(coord.x) / f"{coord.y}.png",
            elevation_url=elevation_export_url(coord),
            elevation_output=pack_root
            / "elevation"
            / str(coord.z)
            / str(coord.x)
            / f"{coord.y}.png",
        )
        for coord in required_coords(manifest)
    ]


def plan_json(manifest: dict[str, Any], pack_root: Path) -> dict[str, Any]:
    items = build_work_items(manifest, pack_root)
    return {
        "schema": "animus.lake_tahoe_pack_plan.v1",
        "tool_version": TOOL_VERSION,
        "pack_root": str(pack_root),
        "tile_ranges": manifest_tile_sets(manifest),
        "imagery": [
            {"tile": item.coord.key, "url": item.imagery_url, "output": str(item.imagery_output)}
            for item in items
        ],
        "elevation": [
            {
                "tile": item.coord.key,
                "url": item.elevation_url,
                "output": str(item.elevation_output),
            }
            for item in items
        ],
    }


def provenance_json(manifest: dict[str, Any], pack_root: Path) -> dict[str, Any]:
    return {
        "schema": PROVENANCE_SCHEMA,
        "tool_version": TOOL_VERSION,
        "created_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "pack_root": str(pack_root),
        "manifest": {
            "schema": manifest.get("schema"),
            "name": manifest.get("name"),
            "tile_set": manifest.get("tile_set"),
            "tile_sets": manifest.get("tile_sets"),
        },
        "sources": {
            "imagery": {
                "name": "USGSImageryOnly cached Web Mercator orthoimagery",
                "url_template": IMAGERY_TILE_URL_TEMPLATE,
            },
            "elevation": {
                "name": "USGS 3DEP Elevation ImageServer",
                "url": ELEVATION_EXPORT_URL,
            },
        },
    }


def fetch_bytes(url: str, timeout_s: float) -> bytes:
    request = Request(url, headers={"User-Agent": "Altair-Animus-TerrainPrep/1"})
    with urlopen(request, timeout=timeout_s) as response:  # noqa: S310 - fixed public USGS URLs.
        status = getattr(response, "status", 200)
        if status != 200:
            raise RuntimeError(f"HTTP {status} for {url}")
        return response.read()


def write_imagery_png(url: str, output: Path, timeout_s: float) -> None:
    from terrain_pack import require_pillow

    Image = require_pillow()
    data = fetch_bytes(url, timeout_s)
    with Image.open(io.BytesIO(data)) as image:
        converted = image.convert("RGB")
        output.parent.mkdir(parents=True, exist_ok=True)
        converted.save(output, format="PNG")


def read_dem_tiff(data: bytes, tile_size: int) -> list[float]:
    try:
        from osgeo import gdal
    except ImportError as exc:
        raise RuntimeError(
            "GDAL Python bindings are required for DEM export decoding. "
            "Install osgeo/gdal before running the live Lake Tahoe downloader."
        ) from exc

    with tempfile.NamedTemporaryFile(suffix=".tif") as temp:
        temp.write(data)
        temp.flush()
        dataset = gdal.Open(temp.name)
        if dataset is None:
            raise RuntimeError("GDAL could not open USGS DEM export")
        try:
            if dataset.RasterXSize != tile_size or dataset.RasterYSize != tile_size:
                raise RuntimeError(
                    "DEM export dimensions "
                    f"{dataset.RasterXSize}x{dataset.RasterYSize} do not match "
                    f"{tile_size}x{tile_size}"
                )
            band = dataset.GetRasterBand(1)
            array = band.ReadAsArray()
            no_data = band.GetNoDataValue()
        finally:
            dataset = None

    values: list[float] = []
    for row in array:
        for sample in row:
            value = float(sample)
            if not math.isfinite(value):
                raise RuntimeError("DEM export contains non-finite pixels")
            if no_data is not None and value == float(no_data):
                raise RuntimeError("DEM export contains no-data pixels")
            values.append(value)
    return values


def write_elevation_png(url: str, output: Path, timeout_s: float, tile_size: int) -> None:
    from terrain_pack import require_pillow

    Image = require_pillow()
    dem_values = read_dem_tiff(fetch_bytes(url, timeout_s), tile_size)
    image = Image.new("RGB", (tile_size, tile_size))
    image.putdata([terrain_rgb_triplet(value) for value in dem_values])
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, format="PNG")


def download_pack(manifest: dict[str, Any], pack_root: Path, timeout_s: float) -> None:
    for item in build_work_items(manifest, pack_root):
        write_imagery_png(item.imagery_url, item.imagery_output, timeout_s)
        write_elevation_png(item.elevation_url, item.elevation_output, timeout_s, TILE_SIZE)

    provenance_path = pack_root / "provenance.json"
    provenance_path.write_text(
        json.dumps(provenance_json(manifest, pack_root), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_manifest_argument(parser)
    parser.add_argument("--pack-root", type=Path, default=Path("animus/data/tiles/lake_tahoe"))
    parser.add_argument(
        "--dry-run", action="store_true", help="Print planned URLs and outputs only."
    )
    parser.add_argument(
        "--timeout", type=float, default=60.0, help="Per-request timeout in seconds."
    )
    args = parser.parse_args()

    manifest = load_manifest(args.manifest)
    if args.dry_run:
        print(json.dumps(plan_json(manifest, args.pack_root), indent=2, sort_keys=True))
        return 0

    download_pack(manifest, args.pack_root, args.timeout)
    errors = validate_pack(args.pack_root, manifest)
    if errors:
        for error in errors:
            print(error)
        return 1
    print(f"prepared Lake Tahoe terrain pack: {args.pack_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

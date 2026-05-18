#!/usr/bin/env python3
"""Generate an Animus Qt MBTiles offline map pack from local rasters."""

from __future__ import annotations

import argparse
import json
import shutil
import sqlite3
import subprocess
from pathlib import Path
from typing import Sequence

DEFAULT_NAME = "Default SITL Stanford / Palo Alto"
DEFAULT_DESCRIPTION = "Offline map pack for Altair Animus SITL/demo use around Stanford/Palo Alto."
DEFAULT_LICENSE = (
    "USGS public domain sources with OSM ODbL attribution where OSM-derived data is included"
)
DEFAULT_ATTRIBUTION = (
    "Map services and data available from U.S. Geological Survey, National Geospatial Program. "
    "© OpenStreetMap contributors where OSM-derived data is included."
)
DEFAULT_CENTER = {"latitude": 37.4275, "longitude": -122.1697}


def _run(command: Sequence[str], dry_run: bool) -> None:
    print(" ".join(command))
    if not dry_run:
        subprocess.run(command, check=True)


def _require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise SystemExit(f"required GDAL tool is not on PATH: {name}")


def _safe_relative(value: str) -> bool:
    path = Path(value)
    return value != "" and not path.is_absolute() and ".." not in path.parts


def _source_manifest(paths: Sequence[Path]) -> list[str]:
    return [str(path) for path in paths]


def _empty_tile_coverage() -> dict[str, object]:
    return {"scheme": "xyz", "zooms": {}}


def _metadata(
    args: argparse.Namespace, *, imagery_tile_coverage: dict[str, object] | None = None
) -> dict[str, object]:
    west, south, east, north = args.bbox
    return {
        "schemaVersion": 1,
        "name": args.name,
        "description": args.description,
        "license": args.license,
        "attribution": args.attribution,
        "minZoom": args.min_zoom,
        "maxZoom": args.max_zoom,
        "bounds": {"west": west, "south": south, "east": east, "north": north},
        "center": {"latitude": args.center[0], "longitude": args.center[1]},
        "imagery": {
            "format": "mbtiles",
            "path": "2d/imagery.mbtiles",
            "extension": "png",
            "sourceStatus": "real-offline-imagery",
            "sourceType": "operator-provided-local-raster",
            "sources": _source_manifest(args.imagery),
            "tileCoverage": imagery_tile_coverage or _empty_tile_coverage(),
        },
        "terrain": {
            "format": "none",
            "status": "staged",
            "preferredFormat": "quantized-mesh",
            "path": "3d/terrain_quantized_mesh",
            "source": str(args.dem) if args.dem is not None else "",
        },
        "topography": {
            "hillshade": "3d/hillshade.mbtiles",
            "contours": "3d/contours/contours.geojson",
        },
        "generation": {
            "inputBounds": {"west": west, "south": south, "east": east, "north": north},
            "minZoom": args.min_zoom,
            "maxZoom": args.max_zoom,
            "processes": args.processes,
            "contourIntervalM": args.contour_interval_m,
        },
        "version": args.version,
        "createdBy": "Altair Animus map-pack generator",
        "createdAt": args.created_at,
    }


def _write_metadata(
    pack_dir: Path,
    args: argparse.Namespace,
    dry_run: bool,
    *,
    imagery_tile_coverage: dict[str, object] | None = None,
) -> None:
    metadata = _metadata(args, imagery_tile_coverage=imagery_tile_coverage)
    if dry_run:
        print(f"would write {pack_dir / 'metadata.json'}")
        print(f"would write {pack_dir / 'attribution.txt'}")
        return
    pack_dir.mkdir(parents=True, exist_ok=True)
    (pack_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=False) + "\n", encoding="utf-8"
    )
    (pack_dir / "attribution.txt").write_text(args.attribution + "\n", encoding="utf-8")


def _validate_inputs(args: argparse.Namespace) -> None:
    west, south, east, north = args.bbox
    if not (-180.0 <= west < east <= 180.0):
        raise SystemExit("bbox west/east must be ordered longitudes within -180..180")
    if not (-85.05112878 <= south < north <= 85.05112878):
        raise SystemExit("bbox south/north must fit Web Mercator latitude range")
    if args.min_zoom < 0 or args.max_zoom < args.min_zoom or args.max_zoom > 30:
        raise SystemExit("zoom range must be ordered and within 0..30")
    if not args.imagery and not args.validate_only:
        raise SystemExit("at least one --imagery source is required unless --validate-only is used")
    for path in args.imagery:
        if not path.is_file():
            raise SystemExit(f"imagery source is missing: {path}")
    if args.dem is not None and not args.dem.is_file():
        raise SystemExit(f"DEM source is missing: {args.dem}")
    if not _safe_relative(args.pack_id):
        raise SystemExit("pack id must be a relative directory name without '..'")


def _pack_xyz_to_mbtiles(
    xyz_root: Path,
    mbtiles_path: Path,
    metadata: dict[str, str],
    dry_run: bool,
) -> dict[str, object]:
    print(f"pack {xyz_root} -> {mbtiles_path}")
    if dry_run:
        return _empty_tile_coverage()

    mbtiles_path.parent.mkdir(parents=True, exist_ok=True)
    if mbtiles_path.exists():
        mbtiles_path.unlink()

    coverage: dict[int, dict[str, int]] = {}
    connection = sqlite3.connect(mbtiles_path)
    try:
        connection.execute("CREATE TABLE metadata (name TEXT, value TEXT)")
        connection.execute(
            "CREATE TABLE tiles ("
            "zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)"
        )
        connection.execute(
            "CREATE UNIQUE INDEX tile_index " "ON tiles (zoom_level, tile_column, tile_row)"
        )
        connection.executemany("INSERT INTO metadata (name, value) VALUES (?, ?)", metadata.items())

        for tile_path in sorted(xyz_root.glob("*/*/*.png")):
            try:
                zoom = int(tile_path.parents[1].name)
                column = int(tile_path.parent.name)
                row = int(tile_path.stem)
            except ValueError:
                continue
            entry = coverage.setdefault(
                zoom,
                {
                    "minX": column,
                    "maxX": column,
                    "minY": row,
                    "maxY": row,
                    "tileCount": 0,
                },
            )
            entry["minX"] = min(entry["minX"], column)
            entry["maxX"] = max(entry["maxX"], column)
            entry["minY"] = min(entry["minY"], row)
            entry["maxY"] = max(entry["maxY"], row)
            entry["tileCount"] += 1
            tms_row = (1 << zoom) - 1 - row
            connection.execute(
                "INSERT INTO tiles (zoom_level, tile_column, tile_row, tile_data) "
                "VALUES (?, ?, ?, ?)",
                (zoom, column, tms_row, tile_path.read_bytes()),
            )
        connection.commit()
    finally:
        connection.close()
    return {
        "scheme": "xyz",
        "zooms": {str(zoom): coverage[zoom] for zoom in sorted(coverage)},
    }


def generate(args: argparse.Namespace) -> None:
    _validate_inputs(args)

    if args.validate_only:
        return

    if not args.dry_run:
        _require_tool("gdalwarp")
        _require_tool("gdal2tiles.py")
        if args.dem is not None:
            _require_tool("gdaldem")
            _require_tool("gdal_contour")

    pack_dir = args.output_root / args.pack_id
    work_dir = args.work_dir / args.pack_id
    imagery_work_dir = work_dir / "imagery"
    terrain_work_dir = work_dir / "terrain"
    imagery_3857 = imagery_work_dir / "imagery_3857.tif"
    imagery_xyz = imagery_work_dir / "xyz"
    dem_3857 = terrain_work_dir / "dem_3857.tif"
    hillshade_3857 = terrain_work_dir / "hillshade_3857.tif"
    hillshade_xyz = terrain_work_dir / "hillshade_xyz"

    if not args.dry_run:
        imagery_work_dir.mkdir(parents=True, exist_ok=True)
        (pack_dir / "2d").mkdir(parents=True, exist_ok=True)

    west, south, east, north = [str(value) for value in args.bbox]
    zoom_range = f"{args.min_zoom}-{args.max_zoom}"
    processes = str(args.processes)

    _run(
        [
            "gdalwarp",
            "-t_srs",
            "EPSG:3857",
            "-te_srs",
            "EPSG:4326",
            "-te",
            west,
            south,
            east,
            north,
            "-r",
            "bilinear",
            *[str(path) for path in args.imagery],
            str(imagery_3857),
        ],
        args.dry_run,
    )
    _run(
        [
            "gdal2tiles.py",
            "--xyz",
            "-z",
            zoom_range,
            "--processes",
            processes,
            str(imagery_3857),
            str(imagery_xyz),
        ],
        args.dry_run,
    )
    imagery_tile_coverage = _pack_xyz_to_mbtiles(
        imagery_xyz,
        pack_dir / "2d" / "imagery.mbtiles",
        {
            "name": args.name,
            "format": "png",
            "attribution": args.attribution,
            "type": "baselayer",
            "version": str(args.version),
        },
        args.dry_run,
    )
    _write_metadata(
        pack_dir,
        args,
        args.dry_run,
        imagery_tile_coverage=imagery_tile_coverage,
    )

    if args.dem is None:
        return

    if not args.dry_run:
        terrain_work_dir.mkdir(parents=True, exist_ok=True)
        (pack_dir / "3d" / "terrain_dem").mkdir(parents=True, exist_ok=True)
        (pack_dir / "3d" / "contours").mkdir(parents=True, exist_ok=True)

    _run(
        [
            "gdalwarp",
            "-t_srs",
            "EPSG:3857",
            "-te_srs",
            "EPSG:4326",
            "-te",
            west,
            south,
            east,
            north,
            "-r",
            "bilinear",
            str(args.dem),
            str(dem_3857),
        ],
        args.dry_run,
    )
    if not args.dry_run:
        shutil.copy2(dem_3857, pack_dir / "3d" / "terrain_dem" / "source_dem.tif")
    else:
        print(f"would copy {dem_3857} to {pack_dir / '3d' / 'terrain_dem' / 'source_dem.tif'}")

    _run(["gdaldem", "hillshade", str(dem_3857), str(hillshade_3857)], args.dry_run)
    _run(
        [
            "gdal2tiles.py",
            "--xyz",
            "-z",
            zoom_range,
            "--processes",
            processes,
            str(hillshade_3857),
            str(hillshade_xyz),
        ],
        args.dry_run,
    )
    _pack_xyz_to_mbtiles(
        hillshade_xyz,
        pack_dir / "3d" / "hillshade.mbtiles",
        {
            "name": f"{args.name} Hillshade",
            "format": "png",
            "attribution": args.attribution,
            "type": "overlay",
            "version": str(args.version),
        },
        args.dry_run,
    )
    _run(
        [
            "gdal_contour",
            "-a",
            "elevation",
            "-i",
            str(args.contour_interval_m),
            str(dem_3857),
            str(pack_dir / "3d" / "contours" / "contours.geojson"),
        ],
        args.dry_run,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-id", required=True)
    parser.add_argument(
        "--bbox",
        nargs=4,
        type=float,
        metavar=("WEST", "SOUTH", "EAST", "NORTH"),
        required=True,
    )
    parser.add_argument("--min-zoom", type=int, required=True)
    parser.add_argument("--max-zoom", type=int, required=True)
    parser.add_argument("--imagery", nargs="+", type=Path, default=[])
    parser.add_argument("--dem", type=Path)
    parser.add_argument("--output-root", type=Path, default=Path("map_packs"))
    parser.add_argument("--work-dir", type=Path, default=Path("build/map-packs"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--processes", type=int, default=8)
    parser.add_argument("--contour-interval-m", type=float, default=10.0)
    parser.add_argument("--name", default=DEFAULT_NAME)
    parser.add_argument("--description", default=DEFAULT_DESCRIPTION)
    parser.add_argument("--license", default=DEFAULT_LICENSE)
    parser.add_argument("--attribution", default=DEFAULT_ATTRIBUTION)
    parser.add_argument(
        "--center",
        nargs=2,
        type=float,
        default=[DEFAULT_CENTER["latitude"], DEFAULT_CENTER["longitude"]],
    )
    parser.add_argument("--version", type=int, default=1)
    parser.add_argument("--created-at", default="1970-01-01T00:00:00Z")
    args = parser.parse_args()
    generate(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

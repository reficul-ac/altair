#!/usr/bin/env python3
"""Validate an Animus Qt offline map pack."""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any

WEB_MERCATOR_LAT_LIMIT = 85.05112878


def _error(errors: list[str], message: str) -> None:
    errors.append(f"error: {message}")


def _warn(warnings: list[str], message: str) -> None:
    warnings.append(f"warning: {message}")


def _is_relative_safe(value: str) -> bool:
    path = Path(value)
    return value != "" and not path.is_absolute() and ".." not in path.parts


def _object(value: Any, name: str, errors: list[str]) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    _error(errors, f"{name} must be an object")
    return {}


def _read_mbtiles_metadata(connection: sqlite3.Connection) -> dict[str, str]:
    try:
        rows = connection.execute("SELECT name, value FROM metadata").fetchall()
    except sqlite3.Error:
        return {}
    return {str(name): str(value) for name, value in rows}


def _validate_mbtiles(
    path: Path,
    metadata: dict[str, Any],
    min_zoom: int | None,
    max_zoom: int | None,
    errors: list[str],
    warnings: list[str],
) -> None:
    if not path.is_file():
        _error(errors, f"MBTiles database is missing: {path}")
        return

    try:
        connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    except sqlite3.Error as exc:
        _error(errors, f"MBTiles database cannot be opened read-only: {exc}")
        return

    try:
        table_names = {
            str(row[0])
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            ).fetchall()
        }
        if "tiles" not in table_names:
            _error(errors, "MBTiles tiles table is missing")
            return

        columns = {str(row[1]) for row in connection.execute("PRAGMA table_info(tiles)").fetchall()}
        required_columns = {"zoom_level", "tile_column", "tile_row", "tile_data"}
        missing_columns = sorted(required_columns - columns)
        if missing_columns:
            _error(errors, f"MBTiles tiles table is missing columns: {', '.join(missing_columns)}")
            return

        tile_count = connection.execute("SELECT count(*) FROM tiles").fetchone()[0]
        if int(tile_count) <= 0:
            _error(errors, "MBTiles tiles table has no tiles")

        if isinstance(min_zoom, int) and isinstance(max_zoom, int):
            for zoom in range(min_zoom, max_zoom + 1):
                count = connection.execute(
                    "SELECT count(*) FROM tiles WHERE zoom_level = ?", (zoom,)
                ).fetchone()[0]
                if int(count) <= 0:
                    _error(errors, f"MBTiles zoom {zoom} has no tiles")

        mbtiles_metadata = _read_mbtiles_metadata(connection)
        fmt = mbtiles_metadata.get("format", "").strip().lower()
        if fmt and fmt not in {"png", "jpg", "jpeg", "webp"}:
            _error(errors, "MBTiles metadata.format must describe raster tiles")
        for field in ("name", "attribution"):
            value = mbtiles_metadata.get(field, "").strip()
            expected = str(metadata.get(field, "")).strip()
            if value and expected and value != expected:
                _error(errors, f"MBTiles metadata.{field} disagrees with metadata.json")
        if "metadata" not in table_names:
            _warn(warnings, "MBTiles metadata table is missing")
    except sqlite3.Error as exc:
        _error(errors, f"MBTiles database is invalid: {exc}")
    finally:
        connection.close()


def validate_pack(pack_dir: Path, require_3d: bool) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []

    metadata_path = pack_dir / "metadata.json"
    if not metadata_path.is_file():
        _error(errors, "metadata.json is missing")
        return errors, warnings

    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        _error(errors, f"metadata.json is not valid JSON: {exc}")
        return errors, warnings
    if not isinstance(metadata, dict):
        _error(errors, "metadata.json must contain a JSON object")
        return errors, warnings

    if metadata.get("schemaVersion") != 1:
        _error(errors, "schemaVersion must be 1")
    for field in ("name", "license", "attribution"):
        if not isinstance(metadata.get(field), str) or not metadata[field].strip():
            _error(errors, f"{field} is required")

    imagery = _object(metadata.get("imagery"), "imagery", errors)
    if imagery.get("format") != "mbtiles":
        _error(errors, 'imagery.format must be "mbtiles"')
    imagery_path_value = imagery.get("path", "2d/imagery.mbtiles")
    if not isinstance(imagery_path_value, str) or not _is_relative_safe(imagery_path_value):
        _error(errors, "imagery.path must be a relative path without '..'")
        imagery_path_value = "2d/imagery.mbtiles"

    min_zoom = metadata.get("minZoom")
    max_zoom = metadata.get("maxZoom")
    if not isinstance(min_zoom, int) or not isinstance(max_zoom, int):
        _error(errors, "minZoom and maxZoom must be integers")
        min_zoom = max_zoom = None
    elif min_zoom < 0 or max_zoom < min_zoom or max_zoom > 30:
        _error(errors, "minZoom and maxZoom must be a valid 0..30 range")

    bounds = _object(metadata.get("bounds"), "bounds", errors)
    try:
        west = float(bounds["west"])
        south = float(bounds["south"])
        east = float(bounds["east"])
        north = float(bounds["north"])
    except (KeyError, TypeError, ValueError):
        _error(errors, "bounds must include numeric west/south/east/north")
    else:
        if not (-180.0 <= west < east <= 180.0):
            _error(errors, "bounds west/east are outside valid longitude order/range")
        if not (-WEB_MERCATOR_LAT_LIMIT <= south < north <= WEB_MERCATOR_LAT_LIMIT):
            _error(errors, "bounds south/north are outside Web Mercator latitude range")

    attribution_path = pack_dir / "attribution.txt"
    if not attribution_path.is_file():
        _error(errors, "attribution.txt is missing")

    _validate_mbtiles(pack_dir / imagery_path_value, metadata, min_zoom, max_zoom, errors, warnings)

    terrain = metadata.get("terrain", {})
    terrain = terrain if isinstance(terrain, dict) else {}
    topo = metadata.get("topography", {})
    topo = topo if isinstance(topo, dict) else {}

    staged_paths = [
        terrain.get("path"),
        topo.get("hillshade"),
        topo.get("contours"),
    ]
    for value in staged_paths:
        if not value:
            continue
        if not isinstance(value, str) or not _is_relative_safe(value):
            _error(errors, f"staged 3D path must be relative and safe: {value!r}")
            continue
        if not (pack_dir / value).exists():
            message = f"staged 3D path is missing: {value}"
            if require_3d:
                _error(errors, message)
            else:
                _warn(warnings, message)

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack_dir", type=Path)
    parser.add_argument(
        "--require-3d",
        action="store_true",
        help="fail when staged terrain/topography paths in metadata are missing",
    )
    args = parser.parse_args()

    errors, warnings = validate_pack(args.pack_dir, args.require_3d)
    for warning in warnings:
        print(warning, file=sys.stderr)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"validated {args.pack_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

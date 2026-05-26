#!/usr/bin/env python3
"""Shared helpers for offline Animus XYZ terrain packs."""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SUPPORTED_LAYERS = {"imagery", "elevation", "bathymetry"}
SUPPORTED_FORMATS = {"png", "terrain_rgb_png", "float32_le"}
TILE_PATH_RE = re.compile(
    r"^(?P<layer>[^/]+)/(?P<z>\d+)/(?P<x>\d+)/(?P<y>\d+)\.(?P<ext>[A-Za-z0-9]+)$"
)


@dataclass(frozen=True, order=True)
class TileCoord:
    z: int
    x: int
    y: int

    @property
    def key(self) -> str:
        return f"{self.z}/{self.x}/{self.y}"


@dataclass(frozen=True)
class TilePath:
    layer: str
    coord: TileCoord
    extension: str
    path: Path


def require_pillow():
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "Pillow is required for PNG inspection. Install Animus tool "
            "requirements with: python3 -m pip install -r animus/tools/requirements.txt"
        ) from exc

    return Image


def web_mercator_bounds(coord: TileCoord) -> dict[str, float]:
    if coord.z < 0:
        raise ValueError("tile zoom must be non-negative")
    axis_count = 1 << coord.z
    if coord.x < 0 or coord.y < 0 or coord.x >= axis_count or coord.y >= axis_count:
        raise ValueError(f"tile coordinate is outside zoom {coord.z}: {coord.key}")

    def lat_from_y(y_index: int) -> float:
        mercator = math.pi * (1.0 - (2.0 * y_index / axis_count))
        return math.degrees(math.atan(math.sinh(mercator)))

    return {
        "south_deg": lat_from_y(coord.y + 1),
        "west_deg": coord.x / axis_count * 360.0 - 180.0,
        "north_deg": lat_from_y(coord.y),
        "east_deg": (coord.x + 1) / axis_count * 360.0 - 180.0,
    }


def parse_tile_path(pack_root: Path, path: Path) -> TilePath:
    relative = path.relative_to(pack_root).as_posix()
    match = TILE_PATH_RE.match(relative)
    if match is None:
        raise ValueError("tile path must match <layer>/<z>/<x>/<y>.<ext>: " f"{relative}")

    layer = match.group("layer")
    if layer not in SUPPORTED_LAYERS:
        raise ValueError(f"unsupported layer '{layer}' in {relative}")

    return TilePath(
        layer=layer,
        coord=TileCoord(
            z=int(match.group("z")),
            x=int(match.group("x")),
            y=int(match.group("y")),
        ),
        extension=match.group("ext").lower(),
        path=path,
    )


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict):
        raise ValueError("manifest root must be a JSON object")
    return manifest


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def lake_tahoe_manifest() -> dict[str, Any]:
    return {
        "schema": "animus.terrain_pack.v1",
        "name": "lake_tahoe_phase_d",
        "description": "Lake Tahoe 3x3 XYZ tile patch for early Animus terrain work.",
        "center": {"lat_deg": 39.0968, "lon_deg": -120.0324},
        "tile_set": {"zoom": 12, "x_min": 681, "x_max": 683, "y_min": 1562, "y_max": 1564},
        "layers": {
            "imagery": {
                "required": True,
                "format": "png",
                "extension": "png",
                "tile_size": 256,
                "sampling_mode": "center",
                "no_data": None,
                "source": (
                    "USGSImageryOnly cached Web Mercator orthoimagery: "
                    "https://basemap.nationalmap.gov/arcgis/rest/services/"
                    "USGSImageryOnly/MapServer"
                ),
            },
            "elevation": {
                "required": True,
                "format": "terrain_rgb_png",
                "extension": "png",
                "tile_size": 256,
                "sampling_mode": "center",
                "no_data": None,
                "height_units": "meters",
                "source": (
                    "USGS 3DEP Elevation ImageServer exported as Terrain-RGB: "
                    "https://elevation.nationalmap.gov/arcgis/rest/services/"
                    "3DEPElevation/ImageServer"
                ),
            },
            "bathymetry": {
                "required": False,
                "format": "float32_le",
                "extension": "f32",
                "tile_size": 256,
                "sampling_mode": "center",
                "no_data": -9999.0,
                "height_units": "meters",
                "source": "optional local bathymetry tile set",
            },
        },
    }


def manifest_tile_sets(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    if "tile_sets" in manifest:
        tile_sets = manifest["tile_sets"]
        if not isinstance(tile_sets, list) or not tile_sets:
            raise ValueError("tile_sets must be a non-empty array")
        return tile_sets
    return [manifest["tile_set"]]


def required_coords(manifest: dict[str, Any]) -> list[TileCoord]:
    coords: list[TileCoord] = []
    for tile_set in manifest_tile_sets(manifest):
        zoom = int(tile_set["zoom"])
        coords.extend(
            TileCoord(zoom, x, y)
            for y in range(int(tile_set["y_min"]), int(tile_set["y_max"]) + 1)
            for x in range(int(tile_set["x_min"]), int(tile_set["x_max"]) + 1)
        )
    return sorted(set(coords))


def iter_tile_files(pack_root: Path) -> list[Path]:
    return sorted(
        path
        for path in pack_root.rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".f32"}
    )


def inspect_png(path: Path) -> dict[str, Any]:
    Image = require_pillow()
    with Image.open(path) as image:
        return {"format": "png", "width": image.width, "height": image.height, "mode": image.mode}


def terrain_rgb_heights(path: Path) -> list[float]:
    Image = require_pillow()
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        heights: list[float] = []
        for red, green, blue in rgb.getdata():
            encoded = red * 256 * 256 + green * 256 + blue
            heights.append(-10000.0 + encoded * 0.1)
        return heights


def inspect_terrain_rgb(path: Path) -> dict[str, Any]:
    png = inspect_png(path)
    heights = terrain_rgb_heights(path)
    png.update({"format": "terrain_rgb_png", "min_m": min(heights), "max_m": max(heights)})
    return png


def float32_values(path: Path) -> list[float]:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        raise ValueError(f"float32 tile byte count is not divisible by 4: {path}")
    count = len(data) // 4
    return list(struct.unpack(f"<{count}f", data))


def inspect_float32(path: Path, tile_size: int | None = None) -> dict[str, Any]:
    values = float32_values(path)
    finite = [value for value in values if math.isfinite(value)]
    if not finite:
        raise ValueError(f"float32 tile has no finite values: {path}")
    result: dict[str, Any] = {
        "format": "float32_le",
        "sample_count": len(values),
        "min_m": min(finite),
        "max_m": max(finite),
    }
    if tile_size is not None:
        expected = tile_size * tile_size
        result["width"] = tile_size
        result["height"] = tile_size
        if len(values) != expected:
            raise ValueError(
                f"float32 tile sample count {len(values)} does not match "
                f"{tile_size}x{tile_size}"
            )
    return result


def inspect_tile(
    pack_root: Path, path: Path, manifest: dict[str, Any] | None = None
) -> dict[str, Any]:
    tile_path = parse_tile_path(pack_root, path)
    layer_spec = (manifest or {}).get("layers", {}).get(tile_path.layer, {})
    declared_format = layer_spec.get("format")
    tile_size = layer_spec.get("tile_size")

    if tile_path.extension == "png" and declared_format == "terrain_rgb_png":
        details = inspect_terrain_rgb(path)
    elif tile_path.extension == "png":
        details = inspect_png(path)
    elif tile_path.extension == "f32":
        details = inspect_float32(path, int(tile_size) if tile_size else None)
    else:
        raise ValueError(f"unsupported tile extension: {tile_path.extension}")

    details.update(
        {
            "layer": tile_path.layer,
            "tile": tile_path.coord.key,
            "path": str(path),
            "byte_size": path.stat().st_size,
            "bounds": web_mercator_bounds(tile_path.coord),
        }
    )
    return details


def validate_manifest(manifest: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if manifest.get("schema") != "animus.terrain_pack.v1":
        errors.append("manifest schema must be animus.terrain_pack.v1")
    if "tile_set" not in manifest and "tile_sets" not in manifest:
        errors.append("manifest must define tile_set or tile_sets")
    if "layers" not in manifest or not isinstance(manifest["layers"], dict):
        errors.append("manifest must define layers object")
        return errors

    for layer, spec in manifest["layers"].items():
        if layer not in SUPPORTED_LAYERS:
            errors.append(f"unsupported layer in manifest: {layer}")
        if spec.get("format") not in SUPPORTED_FORMATS:
            errors.append(f"{layer}: unsupported format {spec.get('format')!r}")
        if "tile_size" not in spec:
            errors.append(f"{layer}: tile_size is required")
        if "sampling_mode" not in spec:
            errors.append(f"{layer}: sampling_mode is required")
        if "no_data" not in spec:
            errors.append(f"{layer}: no_data policy is required")
    return errors


def validate_tile_file(
    pack_root: Path,
    path: Path,
    manifest: dict[str, Any],
    spec: dict[str, Any],
) -> list[str]:
    errors: list[str] = []
    tile_size = int(spec["tile_size"])
    try:
        details = inspect_tile(pack_root, path, manifest)
        width = details.get("width")
        height = details.get("height")
        if width is not None and int(width) != tile_size:
            errors.append(f"{path}: width {width} does not match {tile_size}")
        if height is not None and int(height) != tile_size:
            errors.append(f"{path}: height {height} does not match {tile_size}")
        if spec["format"] in {"terrain_rgb_png", "float32_le"}:
            if not math.isfinite(float(details["min_m"])):
                errors.append(f"{path}: min elevation is not finite")
            if not math.isfinite(float(details["max_m"])):
                errors.append(f"{path}: max elevation is not finite")
    except Exception as exc:  # noqa: BLE001 - CLI validation reports all file errors.
        errors.append(f"{path}: {exc}")
    return errors


def validate_pack(pack_root: Path, manifest: dict[str, Any]) -> list[str]:
    errors = validate_manifest(manifest)
    if errors:
        return errors

    files_by_key: dict[tuple[str, str], Path] = {}
    tile_sets = manifest_tile_sets(manifest)

    def covered(coord: TileCoord) -> bool:
        for tile_set in tile_sets:
            if coord.z != int(tile_set["zoom"]):
                continue
            if int(tile_set["x_min"]) <= coord.x <= int(tile_set["x_max"]) and int(
                tile_set["y_min"]
            ) <= coord.y <= int(tile_set["y_max"]):
                return True
        return False

    for path in iter_tile_files(pack_root):
        try:
            tile_path = parse_tile_path(pack_root, path)
        except ValueError as exc:
            errors.append(str(exc))
            continue

        layer_spec = manifest["layers"].get(tile_path.layer)
        if layer_spec is None:
            errors.append(f"{path}: layer is not declared in manifest")
            continue
        if tile_path.extension != layer_spec["extension"].lower():
            errors.append(f"{path}: extension does not match manifest layer extension")
        if not covered(tile_path.coord):
            errors.append(f"{path}: tile is outside manifest coordinate coverage")
        errors.extend(validate_tile_file(pack_root, path, manifest, layer_spec))

        files_by_key[(tile_path.layer, tile_path.coord.key)] = path

    for layer, spec in manifest["layers"].items():
        if not spec.get("required", False):
            continue
        expected_ext = f".{spec['extension'].lower()}"
        for coord in required_coords(manifest):
            key = (layer, coord.key)
            path = files_by_key.get(key)
            if path is None:
                errors.append(f"missing required tile: {layer}/{coord.key}{expected_ext}")
                continue
            if path.suffix.lower() != expected_ext:
                errors.append(f"{path}: expected extension {expected_ext}")
    return errors


def add_manifest_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("animus/data/sample_areas/lake_tahoe_phase_d.json"),
        help="Terrain pack manifest path.",
    )

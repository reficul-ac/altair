#!/usr/bin/env python3
"""Capture deterministic screenshots from the experimental Qt Animus shell."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKSPACES = ("map-2d", "terrain-3d", "fpv", "tactical", "setup")
XVFB_SCREEN = "1280x820x24"
EXPECTED_CAPTURE_SIZE = (1280, 820)


@dataclass(frozen=True)
class PngImage:
    width: int
    height: int
    channels: int
    color_type: int
    palette: tuple[tuple[int, int, int], ...]
    rows: tuple[bytes, ...]


@dataclass(frozen=True)
class RegionSpec:
    name: str
    bounds: tuple[float, float, float, float]
    min_colors: int
    min_luminance_range: float
    max_dominant_fraction: float


WORKSPACE_COLOR_MINIMUMS = {
    "map-2d": 8,
    "terrain-3d": 18,
    "terrain-3d-workspace": 18,
    "fpv": 18,
    "fpv-workspace": 18,
    "tactical": 12,
    "tactical-workspace": 10,
    "setup": 7,
}

WORKSPACE_CONTENT_REGIONS = {
    "map-2d": (
        RegionSpec("map canvas", (0.06, 0.20, 0.94, 0.88), 6, 10.0, 0.96),
        RegionSpec("vehicle/home overlays", (0.40, 0.34, 0.60, 0.60), 4, 18.0, 0.92),
    ),
    "terrain-3d": (
        RegionSpec("native Cesium terrain canvas", (0.04, 0.06, 0.96, 0.94), 18, 28.0, 0.90),
        RegionSpec("vehicle and trail", (0.36, 0.22, 0.66, 0.62), 5, 30.0, 0.90),
    ),
    "terrain-3d-workspace": (
        RegionSpec("toolbar and tabs", (0.0, 0.0, 1.0, 0.15), 7, 18.0, 0.96),
        RegionSpec("terrain workspace scene", (0.04, 0.18, 0.96, 0.84), 18, 28.0, 0.92),
        RegionSpec("clearance overlay", (0.01, 0.70, 0.36, 0.98), 5, 20.0, 0.94),
    ),
    "fpv": (),
    "fpv-workspace": (RegionSpec("toolbar and tabs", (0.0, 0.0, 1.0, 0.15), 7, 18.0, 0.96),),
    "tactical": (
        RegionSpec("tactical attitude canvas", (0.04, 0.08, 0.96, 0.94), 10, 28.0, 0.97),
        RegionSpec("attitude reference and aircraft", (0.32, 0.22, 0.74, 0.72), 8, 24.0, 0.96),
    ),
    "tactical-workspace": (
        RegionSpec("toolbar and tabs", (0.0, 0.0, 1.0, 0.15), 7, 18.0, 0.96),
        RegionSpec("tactical workspace scene", (0.04, 0.18, 0.96, 0.84), 10, 28.0, 0.97),
        RegionSpec("attitude overlay", (0.01, 0.14, 0.34, 0.58), 3, 3.0, 0.995),
    ),
    "setup": (
        RegionSpec("setup controls", (0.02, 0.18, 0.98, 0.68), 3, 18.0, 0.94),
        RegionSpec("map policy panel", (0.02, 0.14, 0.98, 0.34), 3, 16.0, 0.94),
    ),
}

TOP_REGION = RegionSpec("toolbar and tabs", (0.0, 0.0, 1.0, 0.15), 7, 18.0, 0.96)
WORKSPACE_DIFFERENCE_MINIMUM = 8.0
EXPECTED_TAB_LABELS = ("Map 2D", "Terrain 3D", "FPV", "Tactical", "Setup")
XCB_FAILURE_MARKERS = (
    "could not connect to display",
    'could not load the qt platform plugin "xcb"',
    "no qt platform plugin could be initialized",
    "xcb",
)
REQUIRED_CONTROL_SURFACES = {
    "left_aileron": "aileron_left_pivot",
    "right_aileron": "aileron_right_pivot",
    "elevator": "elevator_pivot",
    "rudder": "rudder_pivot",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--executable",
        default=str(REPO_ROOT / "build-animus-qt" / "tools" / "animus-qt" / "animus_qt"),
        help="Path to the built animus_qt executable.",
    )
    parser.add_argument(
        "--artifacts-dir",
        default=str(REPO_ROOT / "artifacts" / "animus-qt-screenshots"),
        help="Directory where capture artifacts are written.",
    )
    parser.add_argument(
        "--capture-delay-ms", type=int, default=1200, help="Render delay before each screenshot."
    )
    parser.add_argument(
        "--theme",
        choices=("light", "dark", "both"),
        default="light",
        help="Theme mode to force during capture.",
    )
    parser.add_argument(
        "--no-run", action="store_true", help="Only write readiness artifacts; do not launch Qt."
    )
    return parser.parse_args()


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_image(path: Path) -> PngImage:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")

    offset = 8
    width = 0
    height = 0
    bit_depth = 0
    color_type = 0
    palette: list[tuple[int, int, int]] = []
    compressed = bytearray()
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_data = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk_data[:10])
        elif chunk_type == b"PLTE":
            palette = [
                tuple(chunk_data[i : i + 3])  # type: ignore[arg-type]
                for i in range(0, len(chunk_data) - 2, 3)
            ]
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width <= 0 or height <= 0:
        raise ValueError("PNG has zero dimensions")
    if bit_depth != 8:
        raise ValueError(f"unsupported PNG bit depth {bit_depth}")

    channels_by_type = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    channels = channels_by_type.get(color_type)
    if channels is None:
        raise ValueError(f"unsupported PNG color type {color_type}")

    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    rows: list[bytes] = []
    pos = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = raw[pos]
        pos += 1
        row = bytearray(raw[pos : pos + stride])
        pos += stride
        for i, value in enumerate(row):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                row[i] = (value + left) & 0xFF
            elif filter_type == 2:
                row[i] = (value + up) & 0xFF
            elif filter_type == 3:
                row[i] = (value + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                row[i] = (value + paeth(left, up, up_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        rows.append(bytes(row))
        previous = row

    return PngImage(width, height, channels, color_type, tuple(palette), tuple(rows))


def pixel_rgb(image: PngImage, x: int, y: int) -> tuple[int, int, int]:
    x = min(max(x, 0), image.width - 1)
    y = min(max(y, 0), image.height - 1)
    row = image.rows[y]
    start = x * image.channels
    value = tuple(row[start : start + image.channels])
    if image.color_type == 3 and value:
        index = value[0]
        return image.palette[index] if index < len(image.palette) else (index, index, index)
    if image.color_type == 0:
        return (value[0], value[0], value[0])
    return (value[0], value[1], value[2])


def sampled_pixels(
    image: PngImage, columns: int = 32, rows: int = 32
) -> list[tuple[int, int, int]]:
    pixels: list[tuple[int, int, int]] = []
    sample_step_x = max(1, image.width // columns)
    sample_step_y = max(1, image.height // rows)
    for y in range(0, image.height, sample_step_y):
        for x in range(0, image.width, sample_step_x):
            pixels.append(pixel_rgb(image, x, y))
    return pixels


def read_png_pixels(path: Path) -> tuple[int, int, list[tuple[int, ...]]]:
    image = read_png_image(path)
    return image.width, image.height, sampled_pixels(image)


def luminance(pixel: tuple[int, int, int]) -> float:
    return 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]


def region_bounds(
    image: PngImage, bounds: tuple[float, float, float, float]
) -> tuple[int, int, int, int]:
    left = min(max(int(round(bounds[0] * image.width)), 0), image.width - 1)
    top = min(max(int(round(bounds[1] * image.height)), 0), image.height - 1)
    right = min(max(int(round(bounds[2] * image.width)), left + 1), image.width)
    bottom = min(max(int(round(bounds[3] * image.height)), top + 1), image.height)
    return left, top, right, bottom


def sample_region(
    image: PngImage, spec: RegionSpec, columns: int = 28, rows: int = 12
) -> list[tuple[int, int, int]]:
    left, top, right, bottom = region_bounds(image, spec.bounds)
    width = right - left
    height = bottom - top
    step_x = max(1, width // columns)
    step_y = max(1, height // rows)
    pixels: list[tuple[int, int, int]] = []
    for y in range(top, bottom, step_y):
        for x in range(left, right, step_x):
            pixels.append(pixel_rgb(image, x, y))
    return pixels


def diagnostic_status(failures: list[str], warnings: list[str]) -> str:
    if failures:
        return "fail"
    if warnings:
        return "warn"
    return "pass"


def inspect_region(image: PngImage, spec: RegionSpec) -> dict[str, object]:
    pixels = sample_region(image, spec)
    colors = {pixel for pixel in pixels}
    luminance_values = [luminance(pixel) for pixel in pixels]
    dominant_fraction = 1.0
    if pixels:
        dominant_fraction = max(pixels.count(color) for color in colors) / len(pixels)
    luminance_range = max(luminance_values) - min(luminance_values) if luminance_values else 0.0
    failures: list[str] = []
    warnings: list[str] = []
    if len(colors) <= 1:
        failures.append("region is blank or single-color")
    elif len(colors) < spec.min_colors:
        warnings.append(f"low color diversity: {len(colors)} < {spec.min_colors}")
    if luminance_range < spec.min_luminance_range:
        warnings.append(
            f"low luminance variation: {luminance_range:.1f} < {spec.min_luminance_range:.1f}"
        )
    if dominant_fraction > spec.max_dominant_fraction:
        warnings.append(f"dominant color covers {dominant_fraction:.1%} of sampled region")
    return {
        "name": spec.name,
        "sampledColors": len(colors),
        "luminanceRange": round(luminance_range, 1),
        "dominantFraction": round(dominant_fraction, 3),
        "status": diagnostic_status(failures, warnings),
        "failures": failures,
        "warnings": warnings,
    }


def inspect_clearance_overlay_panel(image: PngImage) -> dict[str, object]:
    spec = RegionSpec("clearance overlay panel", (0.01, 0.70, 0.36, 0.98), 5, 20.0, 0.94)
    pixels = sample_region(image, spec, columns=64, rows=28)
    light_panel_pixels = [
        pixel
        for pixel in pixels
        if pixel[0] >= 225 and pixel[1] >= 225 and pixel[2] >= 215 and max(pixel) - min(pixel) <= 28
    ]
    dark_panel_pixels = [
        pixel
        for pixel in pixels
        if pixel[0] <= 55
        and pixel[1] <= 70
        and pixel[2] <= 85
        and max(pixel) - min(pixel) <= 38
        and luminance(pixel) >= 20.0
    ]
    state_pixels = [
        pixel
        for pixel in pixels
        if max(pixel) - min(pixel) >= 60 and luminance(pixel) <= 170.0 and max(pixel) >= 85
    ]
    panel_fraction = (
        (len(light_panel_pixels) + len(dark_panel_pixels)) / len(pixels) if pixels else 0.0
    )
    state_fraction = len(state_pixels) / len(pixels) if pixels else 0.0
    failures: list[str] = []
    warnings: list[str] = []
    if panel_fraction < 0.10:
        failures.append(f"overlay panel covers {panel_fraction:.1%} of sampled region")
    if state_fraction < 0.005:
        failures.append(f"clearance state accent covers {state_fraction:.1%} of sampled region")
    return {
        "name": spec.name,
        "sampledColors": len({pixel for pixel in pixels}),
        "panelFraction": round(panel_fraction, 3),
        "stateAccentFraction": round(state_fraction, 3),
        "status": diagnostic_status(failures, warnings),
        "failures": failures,
        "warnings": warnings,
    }


def inspect_png(
    path: Path,
    workspace: str = "",
    expected_size: tuple[int, int] = EXPECTED_CAPTURE_SIZE,
    seeded_raster: bool = False,
) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "exists": path.exists(), "diagnostics": []}
    failures: list[str] = []
    warnings: list[str] = []
    if not path.exists():
        failures.append("missing screenshot")
        result.update(
            {
                "ok": False,
                "warning": "missing screenshot",
                "failures": failures,
                "warnings": warnings,
            }
        )
        return result
    try:
        image = read_png_image(path)
        pixels = sampled_pixels(image)
        colors = {pixel for pixel in pixels}
        width = image.width
        height = image.height
        result.update({"width": width, "height": height, "sampledColors": len(colors)})
        if expected_size and (width, height) != expected_size:
            failures.append(
                f"unexpected dimensions: {width}x{height}, expected {expected_size[0]}x{expected_size[1]}"
            )
        if len(colors) <= 1 and workspace == "fpv":
            warnings.append(
                "FPV native camera screenshot is visually flat; relying on camera diagnostics"
            )
        elif len(colors) <= 1:
            failures.append("blank or single-color screenshot")
        elif workspace:
            minimum = WORKSPACE_COLOR_MINIMUMS.get(workspace, 6)
            if len(colors) < minimum:
                warnings.append(f"low screenshot color diversity: {len(colors)} < {minimum}")

        diagnostics = (
            []
            if workspace in ("terrain-3d", "fpv", "tactical")
            else [inspect_region(image, TOP_REGION)]
        )
        for spec in WORKSPACE_CONTENT_REGIONS.get(workspace, ()):
            diagnostics.append(inspect_region(image, spec))
        if workspace == "terrain-3d-workspace":
            diagnostics.append(inspect_clearance_overlay_panel(image))
        if workspace == "fpv":
            diagnostics.append(inspect_fpv_visuals(image))
        if workspace == "tactical":
            diagnostics.append(inspect_tactical_visuals(image))
        if seeded_raster:
            diagnostics.append(
                inspect_region(
                    image,
                    RegionSpec("seeded raster tiles", (0.08, 0.20, 0.92, 0.88), 16, 22.0, 0.86),
                )
            )
        result["diagnostics"] = diagnostics
        for diagnostic in diagnostics:
            for message in diagnostic.get("failures", []):
                failures.append(f"{diagnostic['name']}: {message}")
            for message in diagnostic.get("warnings", []):
                warnings.append(f"{diagnostic['name']}: {message}")
    except (OSError, ValueError, zlib.error) as exc:
        failures.append(str(exc))

    result["ok"] = not failures
    result["failures"] = failures
    result["warnings"] = warnings
    if failures:
        result["warning"] = "; ".join(failures)
    elif warnings:
        result["warning"] = "; ".join(warnings)
    return result


def inspect_tactical_visuals(image: PngImage) -> dict[str, object]:
    failures: list[str] = []
    warnings: list[str] = []
    x0 = int(image.width * 0.18)
    x1 = int(image.width * 0.92)
    y0 = int(image.height * 0.12)
    y1 = int(image.height * 0.88)
    total = 0
    black = 0
    red = 0
    green = 0
    blue = 0
    yellow = 0
    bright_reference = 0
    nonblack = 0
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            r, g, b = pixel_rgb(image, x, y)
            total += 1
            if r < 24 and g < 24 and b < 24:
                black += 1
            else:
                nonblack += 1
            if r > 140 and g < 95 and b < 95:
                red += 1
            if g > 115 and r < 110 and b < 125:
                green += 1
            if b > 145 and r < 115 and g < 140:
                blue += 1
            if r > 170 and g > 130 and b < 80:
                yellow += 1
            if max(r, g, b) - min(r, g, b) > 55 and max(r, g, b) > 95:
                bright_reference += 1

    if total == 0:
        failures.append("empty tactical visual sample")
    else:
        black_ratio = black / total
        if black_ratio < 0.20:
            warnings.append(f"black background coverage {black_ratio:.2f} < 0.20")
        if black_ratio > 0.94:
            failures.append(f"tactical scene is mostly empty black: {black_ratio:.2f}")
        if nonblack < 180:
            failures.append(f"too few tactical scene reference pixels: {nonblack}")
        if bright_reference < 120:
            failures.append(f"too few bright tactical instrument pixels: {bright_reference}")
    if red < 12:
        warnings.append(f"missing red roll ring pixels: {red}")
    if green < 12:
        warnings.append(f"missing green pitch ring pixels: {green}")
    if blue < 12:
        warnings.append(f"missing blue yaw ring pixels: {blue}")
    if yellow > 20:
        warnings.append(f"unexpected yellow tactical reference pixels: {yellow}")

    return {
        "name": "tactical RGB attitude references",
        "status": diagnostic_status(failures, warnings),
        "failures": failures,
        "warnings": warnings,
        "blackPixels": black,
        "nonblackPixels": nonblack,
        "brightReferencePixels": bright_reference,
        "redPixels": red,
        "greenPixels": green,
        "bluePixels": blue,
        "yellowPixels": yellow,
    }


def inspect_fpv_visuals(image: PngImage) -> dict[str, object]:
    failures: list[str] = []
    warnings: list[str] = []
    x0 = int(image.width * 0.40)
    x1 = int(image.width * 0.60)
    y0 = int(image.height * 0.34)
    y1 = int(image.height * 0.66)
    total = 0
    aircraft_like = 0
    terrain_sky = 0
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            r, g, b = pixel_rgb(image, x, y)
            total += 1
            if b > 145 and r < 80 and g < 150:
                aircraft_like += 1
            if (g >= 90 and r >= 70 and b <= 210) or (b >= 125 and g >= 120):
                terrain_sky += 1

    if total == 0:
        failures.append("empty FPV center sample")
    else:
        aircraft_fraction = aircraft_like / total
        terrain_sky_fraction = terrain_sky / total
        if aircraft_fraction > 0.04:
            failures.append(
                f"ownship-like blue pixels cover {aircraft_fraction:.1%} of central view"
            )
        if terrain_sky_fraction < 0.25:
            failures.append(
                f"terrain/sky pixels cover only {terrain_sky_fraction:.1%} of central view"
            )

    return {
        "name": "FPV central view",
        "status": diagnostic_status(failures, warnings),
        "failures": failures,
        "warnings": warnings,
        "aircraftLikePixels": aircraft_like,
        "terrainSkyPixels": terrain_sky,
    }


def thumbnail_pixels(
    image: PngImage, columns: int = 32, rows: int = 24
) -> list[tuple[int, int, int]]:
    pixels: list[tuple[int, int, int]] = []
    for row in range(rows):
        y = min(image.height - 1, int((row + 0.5) * image.height / rows))
        for column in range(columns):
            x = min(image.width - 1, int((column + 0.5) * image.width / columns))
            pixels.append(pixel_rgb(image, x, y))
    return pixels


def mean_pixel_delta(left: list[tuple[int, int, int]], right: list[tuple[int, int, int]]) -> float:
    count = min(len(left), len(right))
    if count == 0:
        return 0.0
    total = 0.0
    for index in range(count):
        total += (
            sum(abs(left[index][channel] - right[index][channel]) for channel in range(3)) / 3.0
        )
    return total / count


def compare_workspace_screenshots(captures: list[dict[str, object]]) -> list[dict[str, object]]:
    images: dict[str, PngImage] = {}
    for capture in captures:
        workspace = str(capture.get("workspace", ""))
        png = capture.get("png", {})
        if not isinstance(png, dict) or not png.get("ok"):
            continue
        try:
            images[workspace] = read_png_image(Path(str(capture.get("screenshot", ""))))
        except (OSError, ValueError, zlib.error):
            continue

    comparisons: list[dict[str, object]] = []
    for index, left_workspace in enumerate(WORKSPACES):
        for right_workspace in WORKSPACES[index + 1 :]:
            if "fpv" in (left_workspace, right_workspace):
                continue
            if left_workspace not in images or right_workspace not in images:
                continue
            difference = mean_pixel_delta(
                thumbnail_pixels(images[left_workspace]), thumbnail_pixels(images[right_workspace])
            )
            failures: list[str] = []
            if difference < WORKSPACE_DIFFERENCE_MINIMUM:
                failures.append(
                    f"screenshots are too similar: {difference:.1f} < {WORKSPACE_DIFFERENCE_MINIMUM:.1f}"
                )
            comparisons.append(
                {
                    "workspaces": [left_workspace, right_workspace],
                    "meanPixelDelta": round(difference, 1),
                    "status": "fail" if failures else "pass",
                    "failures": failures,
                }
            )
    return comparisons


def inspect_control_surface_diagnostic(
    path: Path, expected_workspace: str | None = None
) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "exists": path.exists()}
    failures: list[str] = []
    if not path.exists():
        failures.append("missing control-surface diagnostic")
        result.update({"ok": False, "failures": failures, "surfaces": []})
        return result

    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        failures.append(f"invalid control-surface diagnostic: {exc}")
        result.update({"ok": False, "failures": failures, "surfaces": []})
        return result

    renderer = payload.get("renderer")
    if renderer != "cesium-webengine":
        failures.append(f"renderer {renderer} is not native cesium-webengine")
    if expected_workspace and payload.get("workspaceMode") != expected_workspace:
        failures.append(f"workspace mode {payload.get('workspaceMode')} != {expected_workspace}")
    if payload.get("profileLoaded") is not True:
        failures.append("vehicle model profile is not loaded")
    if payload.get("modelLoaded") is not True:
        failures.append("vehicle model is not loaded")
    if expected_workspace == "fpv":
        if payload.get("cameraMode") != "fpv":
            failures.append(f"camera mode {payload.get('cameraMode')} != fpv")
        if payload.get("terrainEnabled") is not True:
            failures.append("FPV terrain is not enabled")
        if payload.get("ownshipHidden") is not True:
            failures.append("FPV ownship is not hidden")
        if payload.get("forwardHemisphereCompliant") is not True:
            failures.append("FPV look vector is outside the forward hemisphere")

    if expected_workspace == "tactical":
        if payload.get("cameraMode") != "tactical":
            failures.append(f"camera mode {payload.get('cameraMode')} != tactical")
        if payload.get("freeRoamAvailable") is not False:
            failures.append("tactical renderer exposes free roam")
        if payload.get("vehicleLocked") is not True:
            failures.append("tactical renderer is not vehicle locked")
        profile_asset = str(payload.get("profileAssetUri", ""))
        loaded_model = str(payload.get("loadedModelUri", ""))
        if not profile_asset:
            failures.append("missing selected profile asset URI")
        if not loaded_model:
            failures.append("missing loaded model URI")
        if profile_asset and loaded_model and profile_asset != loaded_model:
            failures.append(
                f"loaded model {loaded_model} does not match selected profile asset {profile_asset}"
            )
        if payload.get("modelMatchesProfileAsset") is not True:
            failures.append("loaded model does not match selected profile asset")

    surfaces = payload.get("surfaces", [])
    by_id = {str(surface.get("id")): surface for surface in surfaces if isinstance(surface, dict)}
    for surface_id, node_name in REQUIRED_CONTROL_SURFACES.items():
        surface = by_id.get(surface_id)
        if not surface:
            failures.append(f"{surface_id}: missing diagnostic entry")
            continue
        if surface.get("node") != node_name:
            failures.append(f"{surface_id}: expected node {node_name}, got {surface.get('node')}")
        if not surface.get("resolved"):
            failures.append(f"{surface_id}: node {node_name} was not resolved")
        if abs(float(surface.get("deflectionDeg", 0.0))) <= 1.0e-6:
            failures.append(f"{surface_id}: expected non-neutral deflection")
        if not surface.get("matrixChanged"):
            failures.append(f"{surface_id}: deflected matrix remained neutral")

    payload_failures = payload.get("failures", [])
    if isinstance(payload_failures, list):
        failures.extend(str(message) for message in payload_failures)
    result.update(payload)
    result["ok"] = not failures
    result["failures"] = failures
    return result


def inspect_chrome_diagnostic(path: Path, expected_workspace: str) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "exists": path.exists()}
    failures: list[str] = []
    if not path.exists():
        failures.append("missing workspace chrome diagnostic")
        result.update({"ok": False, "failures": failures})
        return result
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        failures.append(f"invalid workspace chrome diagnostic: {exc}")
        result.update({"ok": False, "failures": failures})
        return result

    if payload.get("selectedWorkspace") != expected_workspace:
        failures.append(
            f"selected workspace {payload.get('selectedWorkspace')} != {expected_workspace}"
        )
    tabs = payload.get("tabs", [])
    labels = [str(tab.get("label", "")) for tab in tabs if isinstance(tab, dict)]
    for label in EXPECTED_TAB_LABELS:
        if label not in labels:
            failures.append(f"missing tab label {label}")
            continue
        tab = next(tab for tab in tabs if isinstance(tab, dict) and tab.get("label") == label)
        if not tab.get("semanticallyVisible"):
            failures.append(f"tab label {label} is not semantically visible")
        if int(tab.get("width", 0)) <= 1 or int(tab.get("height", 0)) <= 1:
            failures.append(f"tab label {label} has invalid geometry")
        if not tab.get("enabled", True):
            failures.append(f"tab label {label} is disabled")
        label_item = tab.get("labelItem")
        if not isinstance(label_item, dict):
            failures.append(f"tab label {label} is missing visible text item")
        else:
            if label_item.get("label") != label:
                failures.append(
                    f"tab label {label} text item has unexpected text {label_item.get('label')}"
                )
            if not label_item.get("semanticallyVisible"):
                failures.append(f"tab label {label} text item is not semantically visible")
            if int(label_item.get("width", 0)) <= 1 or int(label_item.get("height", 0)) <= 1:
                failures.append(f"tab label {label} text item has invalid geometry")
        if not tab.get("labelTextMatches"):
            failures.append(f"tab label {label} text item does not match the tab text")
        if not tab.get("labelInsideTab"):
            failures.append(f"tab label {label} text item is outside the tab bounds")
    chrome = payload.get("chrome", {})
    if isinstance(chrome, dict) and not chrome.get("semanticallyVisible"):
        failures.append("workspace chrome is not semantically visible")
    theme_mode = payload.get("themeMode")
    if theme_mode not in ("light", "dark"):
        failures.append(f"unexpected theme mode {theme_mode}")
    settings = payload.get("settingsDisclosure")
    if not isinstance(settings, dict):
        failures.append("missing settings disclosure diagnostic")
    elif not settings.get("semanticallyVisible"):
        failures.append("settings disclosure is not semantically visible")
    link_status = payload.get("linkStatus")
    if not isinstance(link_status, dict):
        failures.append("missing link status diagnostic")
    elif not link_status.get("semanticallyVisible"):
        failures.append("link status is not semantically visible")
    authority = payload.get("authority")
    if not isinstance(authority, dict):
        failures.append("missing command authority diagnostic")
    elif not authority.get("semanticallyVisible"):
        failures.append("command authority is not semantically visible")
    result.update(payload)
    result["ok"] = not failures
    result["failures"] = failures
    return result


def inspect_clearance_diagnostic(path: Path) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "exists": path.exists()}
    failures: list[str] = []
    if not path.exists():
        failures.append("missing terrain clearance diagnostic")
        result.update({"ok": False, "failures": failures})
        return result
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        failures.append(f"invalid terrain clearance diagnostic: {exc}")
        result.update({"ok": False, "failures": failures})
        return result
    for key in (
        "aglM",
        "homeRelativeAltitudeM",
        "terrainHeightM",
        "terrainReportValid",
        "trendMps",
        "minimumRecentClearanceM",
        "state",
        "message",
    ):
        if key not in payload:
            failures.append(f"missing clearance key {key}")
    if payload.get("state") not in ("unknown", "clear", "caution", "warning"):
        failures.append(f"unexpected clearance state {payload.get('state')}")
    result.update(payload)
    result["ok"] = not failures
    result["failures"] = failures
    return result


def inspect_tactical_camera_diagnostic(path: Path) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "ok": False, "failures": []}
    failures: list[str] = []
    if not path.exists():
        result["failures"] = ["missing tactical camera diagnostic"]
        return result
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        result["failures"] = [f"invalid tactical camera diagnostic: {exc}"]
        return result
    if payload.get("renderer") != "cesium-webengine":
        failures.append(f"renderer {payload.get('renderer')} is not native cesium-webengine")
    if payload.get("workspaceMode") != "tactical":
        failures.append(f"workspace mode {payload.get('workspaceMode')} != tactical")
    if payload.get("mode") != "tactical":
        failures.append(f"camera mode {payload.get('mode')} != tactical")
    if payload.get("freeRoamAvailable") is not False:
        failures.append("tactical camera exposes free roam")
    if payload.get("vehicleLocked") is not True:
        failures.append("tactical camera is not vehicle locked")
    result.update(payload)
    result["ok"] = not failures
    result["failures"] = failures
    return result


def inspect_fpv_camera_diagnostic(path: Path) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "ok": False, "failures": []}
    failures: list[str] = []
    if not path.exists():
        result["failures"] = ["missing FPV camera diagnostic"]
        return result
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        result["failures"] = [f"invalid FPV camera diagnostic: {exc}"]
        return result
    if payload.get("renderer") != "cesium-webengine":
        failures.append(f"renderer {payload.get('renderer')} is not native cesium-webengine")
    if payload.get("workspaceMode") != "fpv":
        failures.append(f"workspace mode {payload.get('workspaceMode')} != fpv")
    if payload.get("mode") != "fpv":
        failures.append(f"camera mode {payload.get('mode')} != fpv")
    if payload.get("freeRoamAvailable") is not False:
        failures.append("FPV camera exposes free roam")
    if payload.get("vehicleLocked") is not True:
        failures.append("FPV camera is not vehicle locked")
    if payload.get("terrainEnabled") is not True:
        failures.append("FPV terrain is not enabled")
    if payload.get("ownshipHidden") is not True:
        failures.append("FPV ownship is not hidden")
    if abs(float(payload.get("fixedFovDeg", 0.0)) - 70.0) > 0.5:
        failures.append(f"fixed FOV {payload.get('fixedFovDeg')} != 70")
    if payload.get("forwardHemisphereCompliant") is not True:
        failures.append("FPV look vector is outside the forward hemisphere")
    if float(payload.get("forwardHemisphereDot", -1.0)) < -1.0e-6:
        failures.append(f"FPV forward dot {payload.get('forwardHemisphereDot')} < 0")
    result.update(payload)
    result["ok"] = not failures
    result["failures"] = failures
    return result


def summarize_log(path: Path, max_lines: int = 6) -> str:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    useful = [line.strip() for line in lines if line.strip()]
    if not useful:
        return ""
    return " | ".join(useful[:max_lines])


def command_for_workspace(
    executable: Path,
    screenshot_dir: Path,
    workspace: str,
    delay_ms: int,
    theme: str = "light",
    map_cache_root: Path | None = None,
    seed_map_cache: bool = False,
) -> list[str]:
    if map_cache_root is None:
        map_cache_root = screenshot_dir / "map-cache"
    command = [
        str(executable),
        "--capture-dir",
        str(screenshot_dir),
        "--capture-workspace",
        workspace,
        "--mock-telemetry",
        "--capture-delay-ms",
        str(delay_ms),
        "--theme",
        theme,
        "--map-cache-root",
        str(map_cache_root),
        "--quit-after-capture",
    ]
    if seed_map_cache:
        command.append("--seed-map-cache-fixture")
    if workspace in ("terrain-3d", "fpv", "tactical"):
        command.append("--verify-terrain-control-surfaces")
    return command


def offscreen_env(base_env: dict[str, str]) -> dict[str, str]:
    env = base_env.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["QT_QUICK_BACKEND"] = "software"
    env["QT_OPENGL"] = "software"
    env.setdefault("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu --disable-software-rasterizer")
    return env


def xcb_startup_failure(log_path: Path) -> bool:
    text = summarize_log(log_path, max_lines=40).lower()
    return any(marker in text for marker in XCB_FAILURE_MARKERS)


def should_retry_offscreen(
    strategy: str, log_path: Path, strategies_len: int, attempt_index: int
) -> bool:
    return (
        strategy == "xvfb-run" and xcb_startup_failure(log_path) and strategies_len == attempt_index
    )


def write_report(run_dir: Path, manifest: dict[str, object]) -> None:
    lines = [
        "# Animus Qt Visual Report",
        "",
        f"- Executable: `{manifest['executable']}`",
        f"- Status: `{manifest['status']}`",
        "",
        "| Workspace | Exit | PNG | Dimensions | Sampled colors | Diagnostics |",
        "| --- | ---: | --- | --- | ---: | --- |",
    ]
    for capture in manifest.get("captures", []):
        png = capture["png"]
        dimensions = "-"
        if png.get("width") and png.get("height"):
            dimensions = f"{png['width']}x{png['height']}"
        diagnostics = []
        for diagnostic in png.get("diagnostics", []):
            diagnostics.append(
                f"{diagnostic.get('name', '-')}: {diagnostic.get('status', '-')}"
                f" ({diagnostic.get('sampledColors', 0)} colors, "
                f"lum {diagnostic.get('luminanceRange', 0)})"
            )
        messages = []
        messages.extend(str(message) for message in png.get("failures", []))
        messages.extend(str(message) for message in png.get("warnings", []))
        chrome = capture.get("chromeDiagnostic")
        if isinstance(chrome, dict):
            diagnostics.append(f"chrome tabs: {'pass' if chrome.get('ok') else 'fail'}")
            messages.extend(str(message) for message in chrome.get("failures", []))
        clearance = capture.get("clearanceDiagnostic")
        if isinstance(clearance, dict):
            diagnostics.append(
                "clearance: "
                f"{'pass' if clearance.get('ok') else 'fail'} "
                f"({clearance.get('state', '-')})"
            )
            messages.extend(str(message) for message in clearance.get("failures", []))
        camera = capture.get("cameraDiagnostic")
        if isinstance(camera, dict):
            diagnostics.append(
                "camera: "
                f"{'pass' if camera.get('ok') else 'fail'} "
                f"({camera.get('renderer', '-')}/{camera.get('mode', '-')})"
            )
            messages.extend(str(message) for message in camera.get("failures", []))
        workspace_png = capture.get("workspacePng")
        if isinstance(workspace_png, dict):
            diagnostics.append(
                "workspace image: " f"{'pass' if workspace_png.get('ok') else 'fail'}"
            )
            messages.extend(str(message) for message in workspace_png.get("failures", []))
            messages.extend(str(message) for message in workspace_png.get("warnings", []))
        attempts = capture.get("attempts", [])
        if attempts:
            diagnostics.append(
                "attempts: "
                + ", ".join(
                    f"{attempt.get('displayStrategy', '-')}/{attempt.get('exitCode', '-')}"
                    for attempt in attempts
                    if isinstance(attempt, dict)
                )
            )
        control_surfaces = capture.get("controlSurfaceDiagnostic")
        if isinstance(control_surfaces, dict):
            diagnostics.append(
                "control surfaces: "
                f"{'pass' if control_surfaces.get('ok') else 'fail'} "
                f"({control_surfaces.get('renderer', '-')}, "
                f"{len(control_surfaces.get('surfaces', []))} surfaces, "
                f"{control_surfaces.get('profileId', '-')}, "
                f"{control_surfaces.get('loadedModelUri', '-')})"
            )
            messages.extend(str(message) for message in control_surfaces.get("failures", []))
        diagnostic_text = "<br>".join(diagnostics + messages) if diagnostics or messages else "-"
        lines.append(
            f"| `{capture['workspace']}` | {capture['exitCode']} | "
            f"{'ok' if png.get('ok') else png.get('warning', 'failed')} | "
            f"{dimensions} | {png.get('sampledColors', 0)} | {diagnostic_text} |"
        )

    comparisons = manifest.get("workspaceComparisons", [])
    if comparisons:
        lines.extend(
            [
                "",
                "## Workspace Differences",
                "",
                "| Workspaces | Mean pixel delta | Status | Notes |",
                "| --- | ---: | --- | --- |",
            ]
        )
        for comparison in comparisons:
            workspaces = comparison.get("workspaces", ["-", "-"])
            failures = comparison.get("failures", [])
            notes = "<br>".join(str(message) for message in failures) if failures else "-"
            lines.append(
                f"| `{workspaces[0]}` vs `{workspaces[1]}` | "
                f"{comparison.get('meanPixelDelta', 0)} | {comparison.get('status', '-')} | {notes} |"
            )

    notes = manifest.get("notes", [])
    if notes:
        lines.extend(["", "## Notes", ""])
        lines.extend(f"- {note}" for note in notes)
    lines.append("")
    (run_dir / "visual-report.md").write_text("\n".join(lines), encoding="utf-8")


def diagnostic_messages(diagnostic: object) -> list[str]:
    if not isinstance(diagnostic, dict):
        return []
    messages: list[str] = []
    messages.extend(str(message) for message in diagnostic.get("failures", []))
    messages.extend(str(message) for message in diagnostic.get("warnings", []))
    return messages


def capture_failure_lines(capture: dict[str, object]) -> list[str]:
    workspace = capture.get("workspace", "-")
    lines: list[str] = []
    exit_code = capture.get("exitCode")
    if exit_code != 0:
        lines.append(f"{workspace}: process exited {exit_code}; log={capture.get('log', '-')}")

    png = capture.get("png")
    if isinstance(png, dict) and not png.get("ok", True):
        lines.append(f"{workspace}: screenshot failed: {'; '.join(diagnostic_messages(png))}")
    workspace_png = capture.get("workspacePng")
    if isinstance(workspace_png, dict) and not workspace_png.get("ok", True):
        lines.append(
            f"{workspace}: workspace screenshot failed: "
            f"{'; '.join(diagnostic_messages(workspace_png))}"
        )

    for label, key in (
        ("chrome", "chromeDiagnostic"),
        ("camera", "cameraDiagnostic"),
        ("control surfaces", "controlSurfaceDiagnostic"),
        ("clearance", "clearanceDiagnostic"),
    ):
        diagnostic = capture.get(key)
        if isinstance(diagnostic, dict) and not diagnostic.get("ok", True):
            lines.append(
                f"{workspace}: {label} failed: {'; '.join(diagnostic_messages(diagnostic))}"
            )
    return lines


def print_failure_summary(run_dir: Path, manifest: dict[str, object]) -> None:
    if manifest.get("status") in ("pass", "ready"):
        return
    print("Animus Qt capture failed diagnostics:", file=sys.stderr)
    print(f"manifest={run_dir / 'run-manifest.json'}", file=sys.stderr)
    print(f"visual_report={run_dir / 'visual-report.md'}", file=sys.stderr)
    for capture in manifest.get("captures", []):
        if not isinstance(capture, dict):
            continue
        for line in capture_failure_lines(capture):
            print(line, file=sys.stderr)
    for comparison in manifest.get("workspaceComparisons", []):
        if not isinstance(comparison, dict) or comparison.get("status") != "fail":
            continue
        workspaces = comparison.get("workspaces", ["-", "-"])
        failures = "; ".join(str(message) for message in comparison.get("failures", []))
        print(
            f"{workspaces[0]} vs {workspaces[1]}: workspace comparison failed: {failures}",
            file=sys.stderr,
        )


def main() -> int:
    args = parse_args()
    run_dir = Path(args.artifacts_dir) / timestamp()
    screenshot_dir = run_dir / "screenshots"
    logs_dir = run_dir / "logs"
    cache_dir = run_dir / "map-cache"
    screenshot_dir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    executable = Path(args.executable)
    env = os.environ.copy()
    base_env = env.copy()
    virtual_display = "existing" if env.get("DISPLAY") else "offscreen"
    xvfb_executable = ""
    display = env.get("DISPLAY", "")
    command_prefix: list[str] = []
    notes: list[str] = []

    if not display:
        xvfb_run = shutil.which("xvfb-run")
        if xvfb_run is not None:
            virtual_display = "xvfb-run"
            xvfb_executable = xvfb_run
            env.setdefault(
                "QTWEBENGINE_CHROMIUM_FLAGS",
                "--ignore-gpu-blocklist --enable-webgl --enable-webgl2 "
                "--use-gl=egl --disable-gpu-sandbox",
            )
            command_prefix = [xvfb_run, "-a"]
        else:
            virtual_display = "offscreen"
            env.setdefault("QT_QPA_PLATFORM", "offscreen")
            env.setdefault("QT_QUICK_BACKEND", "software")
            env.setdefault("QT_OPENGL", "software")
            note = "xvfb-run executable not found; falling back to Qt offscreen"
            notes.append(note)
            with (logs_dir / "virtual-display.log").open("a", encoding="utf-8") as log:
                log.write(note + "\n")

    manifest: dict[str, object] = {
        "schemaVersion": 2,
        "createdAt": run_dir.name,
        "executable": str(executable),
        "executableExists": executable.exists(),
        "display": display,
        "virtualDisplay": virtual_display,
        "xvfbExecutable": xvfb_executable,
        "usedXvfb": virtual_display in ("managed-xvfb", "xvfb-run"),
        "usedOffscreen": virtual_display == "offscreen",
        "captures": [],
        "captureAttempts": [],
        "screenshots": [],
        "workspaceComparisons": [],
        "notes": notes,
        "status": "pass",
    }

    if args.no_run:
        manifest["status"] = "ready"
        manifest["notes"].append("no-run requested; Qt app was not launched")
    elif not executable.exists():
        manifest["status"] = "fail"
        manifest["notes"].append(
            "animus_qt executable not found; build with -DALTAIR_BUILD_ANIMUS_QT=ON first"
        )
    else:

        def run_capture(
            workspace: str,
            capture_label: str,
            output_dir: Path,
            map_cache_root: Path,
            theme: str,
            seed_map_cache: bool = False,
        ) -> dict[str, object]:
            output_dir.mkdir(parents=True, exist_ok=True)
            map_cache_root.mkdir(parents=True, exist_ok=True)
            command = command_for_workspace(
                executable,
                output_dir,
                workspace,
                args.capture_delay_ms,
                theme,
                map_cache_root,
                seed_map_cache,
            )
            strategies: list[tuple[str, list[str], dict[str, str]]] = [
                (virtual_display, command_prefix, env)
            ]
            attempts: list[dict[str, object]] = []
            completed: subprocess.CompletedProcess[str] | None = None
            webengine_retry_added = False
            for attempt_index, (strategy, prefix, attempt_env) in enumerate(strategies, start=1):
                launched_command = prefix + command
                log_path = logs_dir / f"{capture_label}-attempt-{attempt_index}.log"
                with log_path.open("w", encoding="utf-8") as log:
                    completed = subprocess.run(
                        launched_command,
                        cwd=REPO_ROOT,
                        env=attempt_env,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        timeout=max(110, args.capture_delay_ms // 1000 + 105),
                        check=False,
                    )
                attempt = {
                    "workspace": workspace,
                    "capture": capture_label,
                    "displayStrategy": strategy,
                    "command": launched_command,
                    "exitCode": completed.returncode,
                    "log": str(log_path),
                    "summary": summarize_log(log_path),
                }
                attempts.append(attempt)
                manifest["captureAttempts"].append(attempt)
                if completed.returncode == 0:
                    break
                if should_retry_offscreen(strategy, log_path, len(strategies), attempt_index):
                    note = (
                        f"{capture_label}: xvfb-run failed during Qt/xcb startup; "
                        "retrying with Qt offscreen software rendering"
                    )
                    notes.append(note)
                    strategies.append(("offscreen-retry", [], offscreen_env(base_env)))
                elif workspace in ("terrain-3d", "fpv", "tactical") and not webengine_retry_added:
                    webengine_retry_added = True
                    note = (
                        f"{capture_label}: native WebEngine capture failed; retrying once "
                        "with the same strict Cesium diagnostics"
                    )
                    notes.append(note)
                    strategies.append((f"{strategy}-webengine-retry", prefix, attempt_env))
            if completed is None:
                raise RuntimeError("capture did not launch")

            expected_size = (
                None if workspace in ("terrain-3d", "fpv", "tactical") else EXPECTED_CAPTURE_SIZE
            )
            png = inspect_png(
                output_dir / f"{workspace}.png",
                workspace,
                expected_size,
                seeded_raster=seed_map_cache,
            )
            capture = {
                "workspace": capture_label,
                "captureWorkspace": workspace,
                "theme": theme,
                "command": attempts[-1]["command"] if attempts else command,
                "exitCode": completed.returncode,
                "log": attempts[-1]["log"] if attempts else "",
                "attempts": attempts,
                "mapCacheRoot": str(map_cache_root),
                "seededMapCacheFixture": seed_map_cache,
                "screenshot": str(output_dir / f"{workspace}.png"),
                "png": png,
            }
            capture["chromeDiagnostic"] = inspect_chrome_diagnostic(
                output_dir / f"{workspace}-chrome.json", workspace
            )
            if workspace in ("terrain-3d", "fpv", "tactical"):
                capture["controlSurfaceDiagnostic"] = inspect_control_surface_diagnostic(
                    output_dir / f"{workspace}-control-surfaces.json", workspace
                )
            if workspace == "fpv":
                capture["cameraDiagnostic"] = inspect_fpv_camera_diagnostic(
                    output_dir / "fpv-camera.json"
                )
            if workspace == "tactical":
                capture["cameraDiagnostic"] = inspect_tactical_camera_diagnostic(
                    output_dir / "tactical-camera.json"
                )
            if workspace == "terrain-3d":
                capture["clearanceDiagnostic"] = inspect_clearance_diagnostic(
                    output_dir / "terrain-3d-clearance.json"
                )
            if workspace in ("terrain-3d", "fpv", "tactical"):
                workspace_png = inspect_png(
                    output_dir / f"{workspace}-workspace.png",
                    f"{workspace}-workspace",
                    EXPECTED_CAPTURE_SIZE,
                    seeded_raster=False,
                )
                capture["workspaceScreenshot"] = str(output_dir / f"{workspace}-workspace.png")
                capture["workspacePng"] = workspace_png
            return capture

        themes = ["light", "dark"] if args.theme == "both" else [args.theme]
        manifest["themes"] = themes
        for theme in themes:
            theme_screenshot_dir = screenshot_dir if len(themes) == 1 else screenshot_dir / theme
            theme_cache_dir = cache_dir if len(themes) == 1 else cache_dir / theme
            for workspace in WORKSPACES:
                capture = run_capture(
                    workspace,
                    workspace if len(themes) == 1 else f"{theme}-{workspace}",
                    theme_screenshot_dir,
                    theme_cache_dir / "fresh",
                    theme,
                    seed_map_cache=False,
                )
                manifest["captures"].append(capture)
                png = capture["png"]
                if png.get("ok"):
                    manifest["screenshots"].append(capture["screenshot"])
                workspace_png = capture.get("workspacePng", {})
                if isinstance(workspace_png, dict) and workspace_png.get("ok"):
                    manifest["screenshots"].append(capture["workspaceScreenshot"])
                chrome_diagnostic = capture.get("chromeDiagnostic", {})
                control_surface_diagnostic = capture.get("controlSurfaceDiagnostic", {})
                clearance_diagnostic = capture.get("clearanceDiagnostic", {})
                camera_diagnostic = capture.get("cameraDiagnostic", {})
                if (
                    capture["exitCode"] != 0
                    or not png.get("ok")
                    or (
                        isinstance(chrome_diagnostic, dict)
                        and not chrome_diagnostic.get("ok", True)
                    )
                    or (
                        isinstance(control_surface_diagnostic, dict)
                        and not control_surface_diagnostic.get("ok", True)
                    )
                    or (
                        isinstance(clearance_diagnostic, dict)
                        and not clearance_diagnostic.get("ok", True)
                    )
                    or (
                        isinstance(camera_diagnostic, dict)
                        and not camera_diagnostic.get("ok", True)
                    )
                    or (isinstance(workspace_png, dict) and not workspace_png.get("ok", True))
                ):
                    manifest["status"] = "fail"
            seeded_capture = run_capture(
                "map-2d",
                "map-2d-seeded-cache" if len(themes) == 1 else f"{theme}-map-2d-seeded-cache",
                theme_screenshot_dir / "seeded-cache",
                theme_cache_dir / "seeded",
                theme,
                seed_map_cache=True,
            )
            manifest["captures"].append(seeded_capture)
            seeded_png = seeded_capture["png"]
            if seeded_png.get("ok"):
                manifest["screenshots"].append(seeded_capture["screenshot"])
            seeded_chrome = seeded_capture.get("chromeDiagnostic", {})
            if (
                seeded_capture["exitCode"] != 0
                or not seeded_png.get("ok")
                or (isinstance(seeded_chrome, dict) and not seeded_chrome.get("ok", True))
            ):
                manifest["status"] = "fail"
        manifest["workspaceComparisons"] = compare_workspace_screenshots(manifest["captures"])
        for comparison in manifest["workspaceComparisons"]:
            if comparison.get("status") == "fail":
                manifest["status"] = "fail"
    (run_dir / "run-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    write_report(run_dir, manifest)
    print(run_dir)
    for path in manifest["screenshots"]:
        print(f"screenshot={path}")
    print_failure_summary(run_dir, manifest)
    return 0 if manifest["status"] in ("pass", "ready") else 2


if __name__ == "__main__":
    sys.exit(main())

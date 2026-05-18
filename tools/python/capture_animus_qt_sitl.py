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
WORKSPACES = ("map-2d", "terrain-3d", "setup")
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
    "setup": 7,
}

WORKSPACE_CONTENT_REGIONS = {
    "map-2d": (
        RegionSpec("map canvas", (0.06, 0.20, 0.94, 0.88), 6, 10.0, 0.96),
        RegionSpec("vehicle/home overlays", (0.40, 0.34, 0.60, 0.60), 4, 18.0, 0.92),
    ),
    "terrain-3d": (
        RegionSpec("terrain preview", (0.05, 0.18, 0.95, 0.90), 18, 28.0, 0.90),
        RegionSpec("vehicle marker", (0.43, 0.38, 0.57, 0.56), 5, 35.0, 0.88),
    ),
    "setup": (
        RegionSpec("setup controls", (0.02, 0.18, 0.98, 0.68), 3, 18.0, 0.94),
        RegionSpec("map policy panel", (0.02, 0.14, 0.98, 0.34), 3, 16.0, 0.94),
    ),
}

TOP_REGION = RegionSpec("toolbar and tabs", (0.0, 0.0, 1.0, 0.15), 7, 18.0, 0.96)
WORKSPACE_DIFFERENCE_MINIMUM = 8.0


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


def inspect_png(
    path: Path, workspace: str = "", expected_size: tuple[int, int] = EXPECTED_CAPTURE_SIZE
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
        if len(colors) <= 1:
            failures.append("blank or single-color screenshot")
        elif workspace:
            minimum = WORKSPACE_COLOR_MINIMUMS.get(workspace, 6)
            if len(colors) < minimum:
                warnings.append(f"low screenshot color diversity: {len(colors)} < {minimum}")

        diagnostics = [inspect_region(image, TOP_REGION)]
        for spec in WORKSPACE_CONTENT_REGIONS.get(workspace, ()):
            diagnostics.append(inspect_region(image, spec))
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
    executable: Path, screenshot_dir: Path, workspace: str, delay_ms: int
) -> list[str]:
    return [
        str(executable),
        "--capture-dir",
        str(screenshot_dir),
        "--capture-workspace",
        workspace,
        "--mock-telemetry",
        "--capture-delay-ms",
        str(delay_ms),
        "--quit-after-capture",
    ]


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


def main() -> int:
    args = parse_args()
    run_dir = Path(args.artifacts_dir) / timestamp()
    screenshot_dir = run_dir / "screenshots"
    logs_dir = run_dir / "logs"
    screenshot_dir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)

    executable = Path(args.executable)
    env = os.environ.copy()
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
            env.setdefault("QT_OPENGL", "software")
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
        for workspace in WORKSPACES:
            command = command_for_workspace(
                executable, screenshot_dir, workspace, args.capture_delay_ms
            )
            launched_command = command_prefix + command
            log_path = logs_dir / f"{workspace}.log"
            with log_path.open("w", encoding="utf-8") as log:
                completed = subprocess.run(
                    launched_command,
                    cwd=REPO_ROOT,
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=max(10, args.capture_delay_ms // 1000 + 10),
                    check=False,
                )
            png = inspect_png(screenshot_dir / f"{workspace}.png", workspace)
            capture = {
                "workspace": workspace,
                "command": launched_command,
                "exitCode": completed.returncode,
                "log": str(log_path),
                "screenshot": str(screenshot_dir / f"{workspace}.png"),
                "png": png,
            }
            manifest["captures"].append(capture)
            if png.get("ok"):
                manifest["screenshots"].append(capture["screenshot"])
            if completed.returncode != 0 or not png.get("ok"):
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
    return 0 if manifest["status"] in ("pass", "ready") else 2


if __name__ == "__main__":
    sys.exit(main())

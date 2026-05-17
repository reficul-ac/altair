#!/usr/bin/env python3
"""Capture deterministic screenshots from the experimental Qt Animus shell."""

from __future__ import annotations

import argparse
import json
import os
import select
import shutil
import signal
import struct
import subprocess
import sys
import zlib
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKSPACES = ("map-2d", "terrain-3d", "setup")
XVFB_SCREEN = "1280x820x24"


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
    parser.add_argument("--capture-delay-ms", type=int, default=1200, help="Render delay before each screenshot.")
    parser.add_argument("--no-run", action="store_true", help="Only write readiness artifacts; do not launch Qt.")
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


def read_png_pixels(path: Path) -> tuple[int, int, list[tuple[int, ...]]]:
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

    pixels: list[tuple[int, ...]] = []
    sample_step_x = max(1, width // 32)
    sample_step_y = max(1, height // 32)
    for y in range(0, height, sample_step_y):
        row = rows[y]
        for x in range(0, width, sample_step_x):
            start = x * channels
            value = tuple(row[start : start + channels])
            if color_type == 3 and value:
                index = value[0]
                value = palette[index] if index < len(palette) else (index, index, index)
            pixels.append(value)
    return width, height, pixels


def inspect_png(path: Path) -> dict[str, object]:
    result: dict[str, object] = {"path": str(path), "exists": path.exists()}
    if not path.exists():
        result["ok"] = False
        result["warning"] = "missing screenshot"
        return result
    try:
        width, height, pixels = read_png_pixels(path)
        colors = {pixel for pixel in pixels}
        result.update(
            {
                "width": width,
                "height": height,
                "sampledColors": len(colors),
                "ok": width > 0 and height > 0 and len(colors) > 1,
            }
        )
        if not result["ok"]:
            result["warning"] = "blank or single-color screenshot"
    except (OSError, ValueError, zlib.error) as exc:
        result["ok"] = False
        result["warning"] = str(exc)
    return result


class ManagedXvfb:
    def __init__(self, executable: str, process: subprocess.Popen[object], display: str) -> None:
        self.executable = executable
        self.process = process
        self.display = display

    def stop(self) -> None:
        if self.process.poll() is not None:
            return
        try:
            os.killpg(self.process.pid, signal.SIGTERM)
            self.process.wait(timeout=3.0)
        except (OSError, subprocess.TimeoutExpired):
            if self.process.poll() is None:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except OSError:
                    pass
                self.process.wait(timeout=1.0)


def summarize_log(path: Path, max_lines: int = 6) -> str:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    useful = [line.strip() for line in lines if line.strip()]
    if not useful:
        return ""
    return " | ".join(useful[:max_lines])


def start_managed_xvfb(log_path: Path, timeout_s: float = 5.0) -> ManagedXvfb:
    xvfb = shutil.which("Xvfb")
    if xvfb is None:
        raise RuntimeError("Xvfb executable not found")

    read_fd, write_fd = os.pipe()
    try:
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(
                [xvfb, "-screen", "0", XVFB_SCREEN, "-nolisten", "tcp", "-displayfd", str(write_fd)],
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
                start_new_session=True,
                pass_fds=(write_fd,),
            )
    finally:
        os.close(write_fd)

    try:
        readable, _, _ = select.select([read_fd], [], [], timeout_s)
        if not readable:
            raise RuntimeError("timed out waiting for Xvfb display allocation")
        display_number = os.read(read_fd, 32).decode("utf-8", errors="replace").strip()
    except Exception:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except OSError:
                pass
            process.wait(timeout=1.0)
        raise
    finally:
        os.close(read_fd)

    if not display_number or process.poll() is not None:
        details = summarize_log(log_path)
        message = "Xvfb failed to start"
        if details:
            message = f"{message}: {details}"
        raise RuntimeError(message)
    return ManagedXvfb(xvfb, process, f":{display_number}")


def command_for_workspace(executable: Path, screenshot_dir: Path, workspace: str, delay_ms: int) -> list[str]:
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
        "| Workspace | Exit | PNG | Dimensions | Sampled colors |",
        "| --- | ---: | --- | --- | ---: |",
    ]
    for capture in manifest.get("captures", []):
        png = capture["png"]
        dimensions = "-"
        if png.get("width") and png.get("height"):
            dimensions = f"{png['width']}x{png['height']}"
        lines.append(
            f"| `{capture['workspace']}` | {capture['exitCode']} | "
            f"{'ok' if png.get('ok') else png.get('warning', 'failed')} | "
            f"{dimensions} | {png.get('sampledColors', 0)} |"
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
    xvfb: ManagedXvfb | None = None
    virtual_display = "existing" if env.get("DISPLAY") else "offscreen"
    xvfb_executable = ""
    display = env.get("DISPLAY", "")
    notes: list[str] = []

    if not display:
        try:
            xvfb = start_managed_xvfb(logs_dir / "virtual-display.log")
            virtual_display = "managed-xvfb"
            xvfb_executable = xvfb.executable
            display = xvfb.display
            env["DISPLAY"] = display
        except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
            virtual_display = "offscreen"
            env.setdefault("QT_QPA_PLATFORM", "offscreen")
            env.setdefault("QT_QUICK_BACKEND", "software")
            note = f"managed Xvfb startup failed; falling back to Qt offscreen: {exc}"
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
        "usedXvfb": virtual_display == "managed-xvfb",
        "usedOffscreen": virtual_display == "offscreen",
        "captures": [],
        "screenshots": [],
        "notes": notes,
        "status": "pass",
    }

    try:
        if args.no_run:
            manifest["status"] = "ready"
            manifest["notes"].append("no-run requested; Qt app was not launched")
        elif not executable.exists():
            manifest["status"] = "fail"
            manifest["notes"].append("animus_qt executable not found; build with -DALTAIR_BUILD_ANIMUS_QT=ON first")
        else:
            for workspace in WORKSPACES:
                command = command_for_workspace(executable, screenshot_dir, workspace, args.capture_delay_ms)
                log_path = logs_dir / f"{workspace}.log"
                with log_path.open("w", encoding="utf-8") as log:
                    completed = subprocess.run(
                        command,
                        cwd=REPO_ROOT,
                        env=env,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        timeout=max(10, args.capture_delay_ms // 1000 + 10),
                        check=False,
                    )
                png = inspect_png(screenshot_dir / f"{workspace}.png")
                capture = {
                    "workspace": workspace,
                    "command": command,
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
    finally:
        if xvfb is not None:
            xvfb.stop()

    (run_dir / "run-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    write_report(run_dir, manifest)
    print(run_dir)
    for path in manifest["screenshots"]:
        print(f"screenshot={path}")
    return 0 if manifest["status"] in ("pass", "ready") else 2


if __name__ == "__main__":
    sys.exit(main())

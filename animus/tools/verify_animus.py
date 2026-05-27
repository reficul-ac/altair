#!/usr/bin/env python3
"""Configure, build, and test the standalone Animus tree."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import struct
import threading
import time
import zlib
from pathlib import Path


def run_command(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print(f"+ {' '.join(command)}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def animus_root() -> Path:
    return Path(__file__).resolve().parents[1]


def conan_command(root: Path) -> str:
    local_conan = root / ".venv" / "bin" / "conan"
    if local_conan.exists():
        return str(local_conan)

    found = shutil.which("conan")
    if found:
        return found

    raise RuntimeError("Conan is required. Install it on PATH or create animus/.venv with Conan.")


def crc_accumulate(byte: int, crc: int) -> int:
    byte ^= crc & 0xFF
    byte ^= (byte << 4) & 0xFF
    return ((crc >> 8) ^ (byte << 8) ^ (byte << 3) ^ (byte >> 4)) & 0xFFFF


def mavlink_crc(data: bytes, crc_extra: int) -> int:
    crc = 0xFFFF
    for byte in data:
        crc = crc_accumulate(byte, crc)
    return crc_accumulate(crc_extra, crc)


def frame_v1(sequence: int, message_id: int, payload: bytes) -> bytes:
    frame = bytearray([0xFE, len(payload), sequence, 1, 1, message_id])
    frame.extend(payload)
    crc_extra = {0: 50, 30: 39, 33: 104, 74: 20}[message_id]
    frame.extend(struct.pack("<H", mavlink_crc(bytes(frame[1:]), crc_extra)))
    return bytes(frame)


def deterministic_tlog_bytes() -> bytes:
    heartbeat = bytes([2, 3, 4, 5, 6, 7, 8, 3, 0])
    pos1 = struct.pack(
        "<IiiiihhhH",
        1000,
        round(39.13006 * 1.0e7),
        round(-119.98125 * 1.0e7),
        1_500_000,
        100_000,
        300,
        400,
        -50,
        12_345,
    )
    attitude = struct.pack("<Iffffff", 1500, 0.1, 0.2, 0.3, 0.0, 0.0, 0.0)
    vfr = struct.pack("<ffhHff", 18.0, 22.5, 270, 50, 1400.0, -0.5)
    pos2 = struct.pack(
        "<IiiiihhhH",
        2000,
        round(39.13025 * 1.0e7),
        round(-119.97975 * 1.0e7),
        1_501_000,
        101_000,
        310,
        390,
        -45,
        12_400,
    )
    return b"".join(
        [
            frame_v1(1, 0, heartbeat),
            frame_v1(2, 33, pos1),
            frame_v1(3, 30, attitude),
            frame_v1(4, 74, vfr),
            frame_v1(5, 33, pos2),
        ]
    )


def write_generated_tlog(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(deterministic_tlog_bytes())


def assert_pixel_diversity(path: Path, pixels: bytes, channels: int) -> None:
    if not pixels:
        raise RuntimeError(f"Capture has no pixels: {path}")
    samples = {
        pixels[offset : offset + channels]
        for offset in range(0, len(pixels) - channels + 1, channels)
    }
    if len(samples) <= 1:
        raise RuntimeError(f"Capture is blank: {path}")
    if len(samples) < 8:
        raise RuntimeError(f"Capture has too little pixel diversity: {path}")


def assert_ppm_nonblank(path: Path) -> None:
    data = path.read_bytes()
    marker = data.find(b"\n255\n")
    if marker < 0:
        raise RuntimeError(f"Capture is not a PPM file: {path}")
    pixels = data[marker + len(b"\n255\n") :]
    assert_pixel_diversity(path, pixels, 3)


def unfilter_png_row(filter_type: int, row: bytes, previous: bytes, bpp: int) -> bytes:
    result = bytearray(row)
    for index, value in enumerate(row):
        left = result[index - bpp] if index >= bpp else 0
        up = previous[index] if previous else 0
        up_left = previous[index - bpp] if previous and index >= bpp else 0
        if filter_type == 0:
            result[index] = value
        elif filter_type == 1:
            result[index] = (value + left) & 0xFF
        elif filter_type == 2:
            result[index] = (value + up) & 0xFF
        elif filter_type == 3:
            result[index] = (value + ((left + up) // 2)) & 0xFF
        elif filter_type == 4:
            predictor = left + up - up_left
            distances = (abs(predictor - left), abs(predictor - up), abs(predictor - up_left))
            if distances[0] <= distances[1] and distances[0] <= distances[2]:
                paeth = left
            elif distances[1] <= distances[2]:
                paeth = up
            else:
                paeth = up_left
            result[index] = (value + paeth) & 0xFF
        else:
            raise RuntimeError(f"Unsupported PNG row filter: {filter_type}")
    return bytes(result)


def assert_png_nonblank(path: Path) -> tuple[bytes, int]:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise RuntimeError(f"Capture is not a PNG file: {path}")

    offset = 8
    width = 0
    height = 0
    bit_depth = 0
    color_type = 0
    compressed = bytearray()
    while offset < len(data):
        if offset + 8 > len(data):
            raise RuntimeError(f"Malformed PNG chunk header: {path}")
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk_data = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
            if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
                raise RuntimeError(f"Unsupported PNG capture format: {path}")
        elif chunk_type == b"IDAT":
            compressed.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    channels = 3 if color_type == 2 else 4
    row_bytes = width * channels
    raw = zlib.decompress(bytes(compressed))
    previous = b""
    pixels = bytearray()
    raw_offset = 0
    for _ in range(height):
        filter_type = raw[raw_offset]
        row = raw[raw_offset + 1 : raw_offset + 1 + row_bytes]
        decoded = unfilter_png_row(filter_type, row, previous, channels)
        pixels.extend(decoded)
        previous = decoded
        raw_offset += row_bytes + 1
    decoded_pixels = bytes(pixels)
    assert_pixel_diversity(path, decoded_pixels, channels)
    return decoded_pixels, channels


def assert_png_contains_telemetry_marker(path: Path) -> None:
    pixels, channels = assert_png_nonblank(path)
    marker_pixels = 0
    for offset in range(0, len(pixels) - channels + 1, channels):
        red = pixels[offset]
        green = pixels[offset + 1]
        blue = pixels[offset + 2]
        red_marker = red >= 220 and green <= 120 and blue <= 120
        selected_blue_marker = red <= 140 and green >= 150 and blue >= 200
        yellow_marker = red >= 200 and green >= 160 and blue <= 130
        if red_marker or selected_blue_marker or yellow_marker:
            marker_pixels += 1
    if marker_pixels < 20:
        raise RuntimeError(f"Capture does not show the telemetry marker: {path}")


def assert_png_differs(before: Path, after: Path, diff_path: Path, min_changed_pixels: int) -> None:
    before_pixels, before_channels = assert_png_nonblank(before)
    after_pixels, after_channels = assert_png_nonblank(after)
    if before_channels != after_channels or len(before_pixels) != len(after_pixels):
        raise RuntimeError("PNG captures have incompatible pixel layouts")

    diff = bytearray()
    changed = 0
    for offset in range(0, len(before_pixels), before_channels):
        delta = 0
        for channel in range(min(3, before_channels)):
            delta = max(
                delta, abs(before_pixels[offset + channel] - after_pixels[offset + channel])
            )
        if delta > 8:
            changed += 1
        diff.extend([delta, 0, 255 - delta])
    write_rgb_png(diff_path, bytes(diff), 3, len(before_pixels) // before_channels)
    if changed < min_changed_pixels:
        raise RuntimeError(
            f"Overlay capture changed only {changed} pixels; expected at least {min_changed_pixels}"
        )


def write_rgb_png(path: Path, pixels: bytes, source_channels: int, pixel_count: int) -> None:
    width = 320
    height = max(1, pixel_count // width)
    if width * height != pixel_count:
        width = pixel_count
        height = 1
    rows = bytearray()
    stride = width * source_channels
    for row in range(height):
        rows.append(0)
        rows.extend(pixels[row * stride : row * stride + stride])
    compressed = zlib.compress(bytes(rows))

    def chunk(kind: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(kind)
        crc = zlib.crc32(data, crc)
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc & 0xFFFFFFFF)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", compressed)
        + chunk(b"IEND", b"")
    )


def write_overlay_fixture(root: Path, path: Path) -> None:
    ppm = path.with_suffix(".ppm")
    width = 32
    height = 32
    pixels = bytearray()
    for row in range(height):
        for col in range(width):
            if (row // 4 + col // 4) % 2 == 0:
                pixels.extend([255, 48, 48])
            else:
                pixels.extend([48, 220, 255])
    ppm.parent.mkdir(parents=True, exist_ok=True)
    ppm.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + bytes(pixels))
    gdal_translate = shutil.which("gdal_translate")
    if gdal_translate is None:
        raise RuntimeError("gdal_translate is required for overlay smoke fixture generation")
    run_command([gdal_translate, "-of", "GTiff", str(ppm), str(path)], cwd=root)


def run_capture_command(
    root: Path,
    executable: Path,
    tlog: Path,
    ppm_capture: Path,
    png_capture: Path | None,
    env: dict[str, str],
    extra_args: list[str] | None = None,
) -> None:
    write_generated_tlog(tlog)

    command = [
        str(executable),
        "--smoke",
        "--frames",
        "120",
        "--telemetry-tlog",
        str(tlog),
        "--capture-ppm",
        str(ppm_capture),
    ]
    if extra_args:
        command.extend(extra_args)
    if png_capture is not None:
        command.extend(["--capture-png", str(png_capture)])
    xvfb_run = shutil.which("xvfb-run")
    if xvfb_run:
        command = [xvfb_run, "-a", *command]
    # Keep ImGui state and relative cache output beside the generated captures.
    run_command(command, cwd=ppm_capture.parent, env=env)
    assert_ppm_nonblank(ppm_capture)
    if png_capture is not None:
        assert_png_contains_telemetry_marker(png_capture)


def send_live_udp_fixture(host: str, port: int, stop: threading.Event) -> None:
    datagrams = [
        frame_v1(
            1,
            33,
            struct.pack(
                "<IiiiihhhH",
                1000,
                round(39.13006 * 1.0e7),
                round(-119.98125 * 1.0e7),
                1_500_000,
                100_000,
                300,
                400,
                -50,
                12_345,
            ),
        ),
        frame_v1(2, 74, struct.pack("<ffhHff", 18.0, 22.5, 270, 50, 1400.0, -0.5)),
        frame_v1(
            3,
            33,
            struct.pack(
                "<IiiiihhhH",
                2000,
                round(39.13025 * 1.0e7),
                round(-119.97975 * 1.0e7),
                1_501_000,
                101_000,
                310,
                390,
                -45,
                12_400,
            ),
        ),
    ]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        while not stop.is_set():
            for datagram in datagrams:
                udp.sendto(datagram, (host, port))
                time.sleep(0.02)


def run_live_udp_smoke(root: Path, build_dir: Path, env: dict[str, str]) -> None:
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")

    host = "127.0.0.1"
    port = 15670
    generated = build_dir / "generated"
    generated.mkdir(parents=True, exist_ok=True)
    ppm_capture = generated / "live_udp_smoke.ppm"
    png_capture = generated / "live_udp_smoke.png"
    command = [
        str(executable),
        "--smoke",
        "--frames",
        "180",
        "--telemetry-live-udp",
        f"{host}:{port}",
        "--capture-ppm",
        str(ppm_capture),
        "--capture-png",
        str(png_capture),
    ]
    xvfb_run = shutil.which("xvfb-run")
    if xvfb_run:
        command = [xvfb_run, "-a", *command]

    stop = threading.Event()
    sender = threading.Thread(target=send_live_udp_fixture, args=(host, port, stop), daemon=True)
    sender.start()
    try:
        run_command(command, cwd=generated, env=env)
    finally:
        stop.set()
        sender.join(timeout=1.0)
    assert_ppm_nonblank(ppm_capture)
    assert_png_contains_telemetry_marker(png_capture)


def run_screenshot_smoke(root: Path, build_dir: Path, env: dict[str, str]) -> None:
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")

    tlog = build_dir / "generated" / "deterministic_smoke.tlog"
    ppm_capture = build_dir / "generated" / "deterministic_smoke.ppm"
    png_capture = build_dir / "generated" / "deterministic_smoke.png"
    run_capture_command(root, executable, tlog, ppm_capture, png_capture, env)


def run_map2d_smoke(root: Path, build_dir: Path, env: dict[str, str]) -> None:
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")

    tlog = build_dir / "generated" / "map2d_smoke.tlog"
    ppm_capture = build_dir / "generated" / "map2d_smoke.ppm"
    png_capture = build_dir / "generated" / "map2d_smoke.png"
    run_capture_command(
        root,
        executable,
        tlog,
        ppm_capture,
        png_capture,
        env,
        extra_args=["--view-mode", "map2d"],
    )


def run_overlay_smoke(root: Path, build_dir: Path, bundle_dir: Path, env: dict[str, str]) -> None:
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")

    overlay = bundle_dir / "overlay_fixture.tif"
    before = bundle_dir / "overlay_before.png"
    after = bundle_dir / "overlay_after.png"
    diff = bundle_dir / "overlay_diff.png"
    write_overlay_fixture(root, overlay)

    def capture(path: Path, extra: list[str]) -> None:
        command = [
            str(executable),
            "--smoke",
            "--frames",
            "120",
            "--no-debug-overlay",
            "--capture-png",
            str(path),
            *extra,
        ]
        xvfb_run = shutil.which("xvfb-run")
        if xvfb_run:
            command = [xvfb_run, "-a", *command]
        run_command(command, cwd=path.parent, env=env)
        assert_png_nonblank(path)

    capture(before, [])
    capture(after, ["--overlay", str(overlay), "--overlay-opacity", "0.85"])
    assert_png_differs(before, after, diff, min_changed_pixels=500)


def write_artifact_bundle(
    root: Path, build_dir: Path, bundle_dir: Path, env: dict[str, str]
) -> None:
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")

    bundle_dir.mkdir(parents=True, exist_ok=True)
    tlog = bundle_dir / "deterministic_smoke.tlog"
    ppm_capture = bundle_dir / "deterministic_smoke.ppm"
    png_capture = bundle_dir / "deterministic_smoke.png"
    run_capture_command(root, executable, tlog, ppm_capture, png_capture, env)

    manifest = {
        "generated_at_unix": int(time.time()),
        "animus_root": str(root),
        "build_dir": str(build_dir),
        "artifacts": {
            "tlog": tlog.name,
            "ppm": ppm_capture.name,
            "png": png_capture.name,
            "html": "index.html",
        },
        "checks": {
            "ppm_pixel_diversity": "passed",
            "png_pixel_diversity": "passed",
            "png_telemetry_marker": "passed",
        },
    }
    (bundle_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (bundle_dir / "index.html").write_text(
        "<!doctype html>\n"
        '<meta charset="utf-8">\n'
        "<title>Animus Verification Artifact</title>\n"
        "<h1>Animus Verification Artifact</h1>\n"
        "<p>Deterministic telemetry screenshot smoke captured as PPM and PNG.</p>\n"
        '<figure><img src="deterministic_smoke.png" alt="Animus PNG capture" '
        'style="max-width:100%;height:auto"></figure>\n'
        "<pre>{}</pre>\n".format(json.dumps(manifest, indent=2)),
        encoding="utf-8",
    )
    print(f"Artifact bundle: {bundle_dir}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure, build, and test the independent Animus project."
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        help="Build directory. Defaults to <animus>/build.",
    )
    parser.add_argument(
        "--screenshot-smoke",
        action="store_true",
        help="Run the app under Xvfb when available and validate nonblank PPM and PNG captures.",
    )
    parser.add_argument(
        "--map2d-smoke",
        action="store_true",
        help="Run the app in Map2D under Xvfb when available and validate capture diversity.",
    )
    parser.add_argument(
        "--artifact-bundle",
        nargs="?",
        const="",
        default=None,
        help=(
            "Write generated tlog, PPM, PNG, manifest, and HTML report. "
            "Defaults to <repo>/artifacts/animus/<timestamp> when no path is supplied."
        ),
    )
    parser.add_argument(
        "--overlay-smoke",
        action="store_true",
        help="Generate a GeoTIFF overlay fixture and verify it visibly changes a PNG capture.",
    )
    parser.add_argument(
        "--live-udp-smoke",
        action="store_true",
        help="Run Animus with direct MAVLink UDP fixture telemetry under Xvfb when available.",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Use the existing build directory and skip Conan, CMake, build, and CTest steps.",
    )
    args = parser.parse_args()

    root = animus_root()
    build_dir = args.build_dir.resolve() if args.build_dir else root / "build"
    toolchain_file = build_dir / "conan_toolchain.cmake"
    conan = conan_command(root)
    conan_home = root / ".conan2"
    env = os.environ.copy()
    env["CONAN_HOME"] = str(conan_home)

    if not args.skip_build:
        cache_file = build_dir / "CMakeCache.txt"
        if cache_file.exists():
            cache_text = cache_file.read_text(encoding="utf-8", errors="ignore")
            if str(toolchain_file) not in cache_text:
                shutil.rmtree(build_dir)

        if not (conan_home / "profiles" / "default").exists():
            run_command([conan, "profile", "detect", "--force"], cwd=root, env=env)

        run_command(
            [
                conan,
                "install",
                str(root),
                "-of",
                str(build_dir),
                "--build=missing",
                "-s",
                "build_type=Debug",
                "-s",
                "compiler.cppstd=20",
            ],
            cwd=root,
            env=env,
        )
        run_command(
            [
                "cmake",
                "-S",
                str(root),
                "-B",
                str(build_dir),
                "-G",
                "Ninja",
                f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
            ],
            cwd=root,
            env=env,
        )
        run_command(["cmake", "--build", str(build_dir)], cwd=root, env=env)
        run_command(
            ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
            cwd=root,
            env=env,
        )
    if args.screenshot_smoke:
        run_screenshot_smoke(root, build_dir, env)
    if args.map2d_smoke:
        run_map2d_smoke(root, build_dir, env)
    if args.live_udp_smoke:
        run_live_udp_smoke(root, build_dir, env)
    if args.artifact_bundle is not None:
        if args.artifact_bundle:
            bundle_dir = Path(args.artifact_bundle).resolve()
        else:
            timestamp = time.strftime("%Y%m%d-%H%M%S")
            bundle_dir = root.parent / "artifacts" / "animus" / timestamp
        write_artifact_bundle(root, build_dir, bundle_dir, env)
        if args.overlay_smoke:
            run_overlay_smoke(root, build_dir, bundle_dir, env)
    elif args.overlay_smoke:
        timestamp = time.strftime("%Y%m%d-%H%M%S")
        bundle_dir = root.parent / "artifacts" / "animus" / timestamp
        bundle_dir.mkdir(parents=True, exist_ok=True)
        run_overlay_smoke(root, build_dir, bundle_dir, env)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

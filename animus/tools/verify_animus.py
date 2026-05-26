#!/usr/bin/env python3
"""Configure, build, and test the standalone Animus tree."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import struct
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
        round(39.1 * 1.0e7),
        round(-120.2 * 1.0e7),
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
        round(39.1005 * 1.0e7),
        round(-120.2005 * 1.0e7),
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


def assert_ppm_nonblank(path: Path) -> None:
    data = path.read_bytes()
    marker = data.find(b"\n255\n")
    if marker < 0:
        raise RuntimeError(f"Capture is not a PPM file: {path}")
    pixels = data[marker + len(b"\n255\n") :]
    if not pixels:
        raise RuntimeError(f"Capture has no pixels: {path}")
    if len(set(pixels)) <= 1:
        raise RuntimeError(f"Capture is blank: {path}")


def run_screenshot_smoke(root: Path, build_dir: Path, env: dict[str, str]) -> None:
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")

    tlog = build_dir / "generated" / "deterministic_smoke.tlog"
    capture = build_dir / "generated" / "deterministic_smoke.ppm"
    write_generated_tlog(tlog)

    command = [
        str(executable),
        "--smoke",
        "--frames",
        "120",
        "--telemetry-tlog",
        str(tlog),
        "--capture-ppm",
        str(capture),
    ]
    xvfb_run = shutil.which("xvfb-run")
    if xvfb_run:
        command = [xvfb_run, "-a", *command]
    run_command(command, cwd=root, env=env)
    assert_ppm_nonblank(capture)


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
        help="Run the app under Xvfb when available and validate a nonblank generated-tlog PPM.",
    )
    args = parser.parse_args()

    root = animus_root()
    build_dir = args.build_dir.resolve() if args.build_dir else root / "build"
    toolchain_file = build_dir / "conan_toolchain.cmake"
    conan = conan_command(root)
    conan_home = root / ".conan2"
    env = os.environ.copy()
    env["CONAN_HOME"] = str(conan_home)

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

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Export a deterministic Animus MP4 through a PNG sequence and FFmpeg."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def run(command: list[str], cwd: Path) -> None:
    print(f"+ {' '.join(command)}", flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def animus_root() -> Path:
    return Path(__file__).resolve().parents[1]


def ffmpeg_supports_encoder(ffmpeg: str, encoder: str) -> bool:
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-encoders"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return encoder in result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--sequence-dir", type=Path, default=None)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--codec", choices=("h264", "av1"), default="h264")
    parser.add_argument(
        "--xvfb",
        choices=("auto", "always", "never"),
        default="auto",
        help="Xvfb usage for headless export; auto uses Xvfb only when DISPLAY is unset.",
    )
    args, animus_args = parser.parse_known_args()

    if args.frames <= 0 or args.fps <= 0:
        raise SystemExit("--frames and --fps must be positive")

    root = animus_root()
    build_dir = args.build_dir.resolve() if args.build_dir else root / "build"
    executable = build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise SystemExit(f"Animus executable not found: {executable}")

    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise SystemExit("ffmpeg is required for MP4 export")

    sequence_dir = (
        args.sequence_dir.resolve()
        if args.sequence_dir
        else (root.parent / "artifacts" / "animus" / "export" / "frames").resolve()
    )
    if sequence_dir.exists():
        shutil.rmtree(sequence_dir)
    sequence_dir.mkdir(parents=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)

    command = [
        str(executable),
        "--smoke",
        "--frames",
        str(args.frames),
        "--width",
        str(args.width),
        "--height",
        str(args.height),
        "--capture-sequence-dir",
        str(sequence_dir),
        "--capture-sequence-fps",
        str(args.fps),
        *animus_args,
    ]
    xvfb_run = shutil.which("xvfb-run")
    use_xvfb = args.xvfb == "always" or (args.xvfb == "auto" and not os.environ.get("DISPLAY"))
    if use_xvfb and xvfb_run:
        command = [xvfb_run, "-a", *command]
    elif use_xvfb and xvfb_run is None:
        raise SystemExit("DISPLAY is unset and xvfb-run is not available")
    run(command, cwd=root)

    if not any(sequence_dir.glob("frame_*.png")):
        raise SystemExit(f"Animus did not write PNG frames in {sequence_dir}")

    if args.codec == "av1":
        if not ffmpeg_supports_encoder(ffmpeg, "libaom-av1"):
            raise SystemExit("installed ffmpeg does not support libaom-av1")
        codec_args = ["-c:v", "libaom-av1", "-crf", "32", "-b:v", "0"]
    else:
        codec_args = ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18"]

    run(
        [
            ffmpeg,
            "-y",
            "-hide_banner",
            "-loglevel",
            "error",
            "-framerate",
            str(args.fps),
            "-i",
            str(sequence_dir / "frame_%06d.png"),
            *codec_args,
            str(args.output),
        ],
        cwd=root,
    )
    if args.output.stat().st_size == 0:
        raise SystemExit(f"MP4 export is empty: {args.output}")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

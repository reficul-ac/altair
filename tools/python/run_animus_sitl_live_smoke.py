#!/usr/bin/env python3
"""Run Altair SITL MAVLink directly into Animus live UDP ingest."""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def import_verify_animus():
    animus_tools = ROOT / "animus" / "tools"
    sys.path.insert(0, str(animus_tools))
    import verify_animus  # pylint: disable=import-error,import-outside-toplevel

    return verify_animus


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Smoke-test direct SITL MAVLink UDP ingestion in Animus."
    )
    parser.add_argument("--animus-build-dir", default="animus/build")
    parser.add_argument("--vehicle-build-dir", default="build")
    parser.add_argument("--output-dir", default=None)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=15680)
    parser.add_argument("--source-port", type=int, default=15681)
    parser.add_argument("--duration", type=float, default=3.0)
    parser.add_argument("--dt", type=float, default=0.02)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--z", type=int, default=12)
    parser.add_argument("--center-x", type=int, default=682)
    parser.add_argument("--center-y", type=int, default=1563)
    return parser.parse_args()


def command_with_xvfb(command: list[str]) -> list[str]:
    xvfb_run = shutil.which("xvfb-run")
    if not xvfb_run:
        return command
    return [xvfb_run, "-a", *command]


def main() -> int:
    args = parse_args()
    verify_animus = import_verify_animus()

    animus_build = (ROOT / args.animus_build_dir).resolve()
    vehicle_build = (ROOT / args.vehicle_build_dir).resolve()
    animus_exe = animus_build / "apps" / "animus" / "animus"
    sitl_exe = vehicle_build / "vehicle" / "sitl_runner"
    if not animus_exe.exists():
        raise SystemExit(f"Animus executable not found: {animus_exe}")
    if not sitl_exe.exists():
        raise SystemExit(f"SITL runner not found: {sitl_exe}")

    if args.output_dir:
        output_dir = pathlib.Path(args.output_dir).resolve()
    else:
        output_dir = ROOT / "artifacts" / "animus-sitl-live" / time.strftime("%Y%m%d-%H%M%S")
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_path = output_dir / "sitl_live.csv"
    debug_csv_path = output_dir / "telemetry_live_debug.csv"
    initial_path = ROOT / "tests" / "integration" / "cruise6dof_initial.ini"
    ppm_path = output_dir / "animus_sitl_live.ppm"
    png_path = output_dir / "animus_sitl_live.png"
    manifest_path = output_dir / "manifest.json"

    animus_command = command_with_xvfb(
        [
            str(animus_exe),
            "--smoke",
            "--frames",
            str(args.frames),
            "--z",
            str(args.z),
            "--center-x",
            str(args.center_x),
            "--center-y",
            str(args.center_y),
            "--telemetry-live-udp",
            f"{args.host}:{args.port}",
            "--telemetry-live-debug-csv",
            str(debug_csv_path),
            "--telemetry-live-render-max-points",
            "1000",
            "--capture-ppm",
            str(ppm_path),
            "--capture-png",
            str(png_path),
        ]
    )
    sitl_command = [
        str(sitl_exe),
        "--scenario",
        "cruise6dof",
        "--initial",
        str(initial_path),
        "--duration",
        str(args.duration),
        "--dt",
        str(args.dt),
        "--output",
        str(csv_path),
        "--mavlink",
        "--mavlink-host",
        args.host,
        "--mavlink-port",
        str(args.port),
    ]
    if args.source_port:
        sitl_command.extend(["--mavlink-source-port", str(args.source_port)])

    animus_log = output_dir / "animus.log"
    sitl_log = output_dir / "sitl.log"
    with animus_log.open("w", encoding="utf-8") as animus_out:
        animus_out.write(f"$ {' '.join(animus_command)}\n\n")
        animus_out.flush()
        animus_process = subprocess.Popen(
            animus_command,
            cwd=output_dir,
            stdout=animus_out,
            stderr=subprocess.STDOUT,
            text=True,
        )

    try:
        time.sleep(1.0)
        with sitl_log.open("w", encoding="utf-8") as sitl_out:
            sitl_out.write(f"$ {' '.join(sitl_command)}\n\n")
            sitl_out.flush()
            sitl_completed = subprocess.run(
                sitl_command,
                cwd=ROOT,
                stdout=sitl_out,
                stderr=subprocess.STDOUT,
                text=True,
                check=False,
            )

        animus_returncode = animus_process.wait(timeout=max(30.0, args.frames))
    except BaseException:
        if animus_process.poll() is None:
            animus_process.terminate()
            try:
                animus_process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                animus_process.kill()
                animus_process.wait()
        raise

    if sitl_completed.returncode != 0:
        raise SystemExit(f"SITL live smoke failed; see {sitl_log}")
    if animus_returncode != 0:
        raise SystemExit(f"Animus live smoke failed; see {animus_log}")

    verify_animus.assert_ppm_nonblank(ppm_path)
    verify_animus.assert_png_contains_telemetry_marker(png_path)

    manifest = {
        "animus_command": animus_command,
        "sitl_command": sitl_command,
        "artifacts": {
            "animus_log": animus_log.name,
            "sitl_log": sitl_log.name,
            "sitl_csv": csv_path.name,
            "telemetry_live_debug_csv": debug_csv_path.name,
            "initial": str(initial_path.relative_to(ROOT)),
            "ppm": ppm_path.name,
            "png": png_path.name,
        },
        "checks": {
            "ppm_pixel_diversity": "passed",
            "png_telemetry_marker": "passed",
        },
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"artifactDir={output_dir}")
    print(f"manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

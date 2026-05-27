#!/usr/bin/env python3
"""Launch Animus and stream cruise6dof SITL MAVLink directly into it."""

from __future__ import annotations

import argparse
import glob
import os
import pathlib
import shlex
import shutil
import signal
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run cruise6dof SITL with direct MAVLink UDP telemetry into Animus."
    )
    parser.add_argument("--animus-build-dir", default="animus/build")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--initial", default=str(ROOT / "tests/integration/cruise6dof_initial.ini"))
    parser.add_argument("--profile", default="cruise")
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", default="sitl_animus_live.csv")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=14550)
    parser.add_argument("--source-port", type=int, default=14551)
    parser.add_argument("--system-id", type=int, default=1)
    parser.add_argument("--z", type=int, default=12)
    parser.add_argument("--center-x", type=int, default=682)
    parser.add_argument("--center-y", type=int, default=1563)
    parser.add_argument("--xvfb", action="store_true", help="run Animus under xvfb-run")
    parser.add_argument(
        "--keep-animus",
        action="store_true",
        help="leave Animus running after SITL exits",
    )
    parser.add_argument(
        "--animus-arg",
        action="append",
        default=[],
        help="extra argument passed to Animus; repeat for multiple arguments",
    )
    parser.add_argument("--dry-run", action="store_true", help="print commands without running")
    return parser.parse_args()


def quote(command: list[str]) -> str:
    return shlex.join(str(part) for part in command)


def command_with_xvfb(command: list[str], enabled: bool) -> list[str]:
    if not enabled:
        return command
    xvfb_run = shutil.which("xvfb-run")
    if not xvfb_run:
        raise RuntimeError("xvfb-run requested but not found on PATH")
    return [xvfb_run, "-a", *command]


def animus_environment() -> dict[str, str]:
    env = os.environ.copy()
    if env.get("DISPLAY"):
        return env

    x11_sockets = sorted(pathlib.Path("/tmp/.X11-unix").glob("X*"))
    if x11_sockets:
        display = ":" + x11_sockets[0].name[1:]
        if any(socket.name == "X0" for socket in x11_sockets):
            display = ":0"
        env["DISPLAY"] = display

    runtime_dir = env.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    auth_files = sorted(glob.glob(str(pathlib.Path(runtime_dir) / ".mutter-Xwaylandauth.*")))
    if auth_files and not env.get("XAUTHORITY"):
        env["XAUTHORITY"] = auth_files[-1]
    return env


def animus_command(args: argparse.Namespace) -> list[str]:
    executable = ROOT / args.animus_build_dir / "apps" / "animus" / "animus"
    if not executable.exists():
        raise RuntimeError(f"Animus executable not found: {executable}")
    command = [
        str(executable),
        "--z",
        str(args.z),
        "--center-x",
        str(args.center_x),
        "--center-y",
        str(args.center_y),
        "--telemetry-live-udp",
        f"{args.host}:{args.port}",
        *args.animus_arg,
    ]
    return command_with_xvfb(command, args.xvfb)


def sitl_command(args: argparse.Namespace) -> list[str]:
    executable = ROOT / args.build_dir / "vehicle" / "sitl_runner"
    if not executable.exists():
        raise RuntimeError(f"SITL runner not found: {executable}")
    command = [
        str(executable),
        "--scenario",
        "cruise6dof",
        "--profile",
        args.profile,
        "--initial",
        args.initial,
        "--duration",
        str(args.duration),
        "--dt",
        str(args.dt),
        "--seed",
        str(args.seed),
        "--output",
        args.output,
        "--mavlink",
        "--mavlink-host",
        args.host,
        "--mavlink-port",
        str(args.port),
        "--mavlink-system-id",
        str(args.system_id),
    ]
    if args.source_port:
        command.extend(["--mavlink-source-port", str(args.source_port)])
    return command


def terminate_process_group(process: subprocess.Popen, timeout_s: float = 3.0) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def main() -> int:
    args = parse_args()
    try:
        animus = animus_command(args)
        sitl = sitl_command(args)
    except RuntimeError as exc:
        print(f"run_animus_sitl_live.py: {exc}", file=sys.stderr)
        return 1

    print(f"animus_live_udp=udp://{args.host}:{args.port}")
    print(f"sitl_source_port={args.source_port}")
    print(f"animus: {quote(animus)}")
    print(f"sitl: {quote(sitl)}")
    if args.dry_run:
        return 0

    animus_process: subprocess.Popen | None = None
    try:
        animus_process = subprocess.Popen(
            animus, cwd=ROOT, env=animus_environment(), start_new_session=True
        )
        time.sleep(1.0)
        if animus_process.poll() is not None:
            return animus_process.returncode or 1
        sitl_result = subprocess.run(sitl, cwd=ROOT, check=False)
        return sitl_result.returncode
    except KeyboardInterrupt:
        return 130
    finally:
        if animus_process is not None and not args.keep_animus:
            terminate_process_group(animus_process)


if __name__ == "__main__":
    raise SystemExit(main())

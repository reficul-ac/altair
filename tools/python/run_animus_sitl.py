#!/usr/bin/env python3
"""Build and run Qt Animus connected to realtime Altair SITL."""

from __future__ import annotations

import argparse
import os
import pathlib
import shlex
import signal
import subprocess
import sys
import time


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Build Qt Animus, open it on UDP telemetry, and run realtime cruise6dof SITL."
    )
    parser.add_argument("--build-dir", default=str(root / "build"))
    parser.add_argument("--animus-build-dir", default=str(root / "build-animus-qt"))
    parser.add_argument(
        "--animus-executable",
        default=None,
        help="path to animus_qt; defaults under --animus-build-dir",
    )
    parser.add_argument("--skip-build", action="store_true", help="do not run CMake/build first")
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=("cruise", "takeoff", "turn", "descent", "failsafe", "mission"),
    )
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", default="sitl_animus.csv", help="CSV log path")
    parser.add_argument(
        "--initial",
        default=str(root / "tests" / "integration" / "cruise6dof_initial.ini"),
        help="initial-condition file",
    )
    parser.add_argument("--case", help="sectioned SITL case file")
    parser.add_argument("--conditions", help="per-step SITL condition file")
    parser.add_argument(
        "--frame-mode", choices=("ned", "ecef"), default="ecef", help="6DOF truth frame"
    )
    parser.add_argument("--udp-host", default="127.0.0.1", help="Animus UDP listen host")
    parser.add_argument("--udp-port", type=int, default=14551, help="Animus UDP listen port")
    parser.add_argument("--mavlink-system-id", type=int, default=1, help="MAVLink system id")
    parser.add_argument(
        "--mavlink-source-port",
        type=int,
        default=14600,
        help="local UDP source port for SITL MAVLink packets",
    )
    parser.add_argument(
        "--animus-start-delay",
        type=float,
        default=1.0,
        help="seconds to wait after launching Animus before starting SITL",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without building or starting processes",
    )
    args = parser.parse_args(argv)
    if args.udp_port < 1 or args.udp_port > 65535:
        parser.error("--udp-port must be in 1..65535")
    if args.mavlink_system_id < 1 or args.mavlink_system_id > 255:
        parser.error("--mavlink-system-id must be in 1..255")
    if args.mavlink_source_port < 0 or args.mavlink_source_port > 65535:
        parser.error("--mavlink-source-port must be in 0..65535")
    if args.case and args.initial != parser.get_default("initial"):
        parser.error("--case and --initial cannot be used together")
    return args


def quote_command(command: list[str]) -> str:
    return shlex.join(str(part) for part in command)


def animus_executable(args: argparse.Namespace) -> pathlib.Path:
    if args.animus_executable:
        return pathlib.Path(args.animus_executable)
    return pathlib.Path(args.animus_build_dir) / "tools" / "animus-qt" / "animus_qt"


def build_commands(args: argparse.Namespace) -> list[list[str]]:
    return [
        ["cmake", "-S", str(repo_root()), "-B", args.build_dir],
        ["cmake", "--build", args.build_dir, "--target", "sitl_runner", "--parallel"],
        [
            "cmake",
            "-S",
            str(repo_root()),
            "-B",
            args.animus_build_dir,
            "-DALTAIR_BUILD_ANIMUS_QT=ON",
        ],
        ["cmake", "--build", args.animus_build_dir, "--target", "animus_qt", "--parallel"],
    ]


def animus_command(args: argparse.Namespace) -> list[str]:
    return [
        str(animus_executable(args)),
        "--start-udp-telemetry",
        "--udp-host",
        args.udp_host,
        "--udp-port",
        str(args.udp_port),
    ]


def sitl_command(args: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        str(repo_root() / "tools" / "python" / "run_sitl.py"),
        "--build-dir",
        args.build_dir,
        "--scenario",
        "cruise6dof",
        "--profile",
        args.profile,
        "--duration",
        str(args.duration),
        "--dt",
        str(args.dt),
        "--seed",
        str(args.seed),
        "--output",
        args.output,
        "--frame-mode",
        args.frame_mode,
        "--realtime",
        "--mavlink",
        "--mavlink-host",
        args.udp_host,
        "--mavlink-port",
        str(args.udp_port),
        "--mavlink-system-id",
        str(args.mavlink_system_id),
    ]
    if args.mavlink_source_port:
        command.extend(["--mavlink-source-port", str(args.mavlink_source_port)])
    if args.case:
        command.extend(["--case", args.case])
    elif args.initial:
        command.extend(["--initial", args.initial])
    if args.conditions:
        command.extend(["--conditions", args.conditions])
    return command


def run_checked(command: list[str]) -> None:
    subprocess.run(command, cwd=repo_root(), check=True)


def start_process(command: list[str]) -> subprocess.Popen:
    return subprocess.Popen(command, cwd=repo_root(), text=True, start_new_session=True)


def terminate(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def print_dry_run(args: argparse.Namespace) -> None:
    if not args.skip_build:
        for index, command in enumerate(build_commands(args), start=1):
            print(f"build {index}: {quote_command(command)}")
    print(f"animus: {quote_command(animus_command(args))}")
    print(f"sitl: {quote_command(sitl_command(args))}")


def run(args: argparse.Namespace) -> int:
    if args.dry_run:
        print_dry_run(args)
        return 0

    try:
        if not args.skip_build:
            for command in build_commands(args):
                print(quote_command(command), flush=True)
                run_checked(command)

        executable = animus_executable(args)
        if not executable.exists():
            raise FileNotFoundError(f"{executable} not found; rerun without --skip-build")

        print(f"animus_udp=udp://{args.udp_host}:{args.udp_port}", flush=True)
        print(f"\n== Start Animus ==\n{quote_command(animus_command(args))}", flush=True)
        animus = start_process(animus_command(args))
        time.sleep(args.animus_start_delay)
        if animus.poll() is not None:
            return animus.returncode or 1

        print(f"\n== Run SITL ==\n{quote_command(sitl_command(args))}", flush=True)
        sitl = start_process(sitl_command(args))
        return sitl.wait()
    except KeyboardInterrupt:
        return 130
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"run_animus_sitl.py: {exc}", file=sys.stderr)
        return 1
    finally:
        terminate(locals().get("sitl"))
        terminate(locals().get("animus"))


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

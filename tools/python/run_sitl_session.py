#!/usr/bin/env python3
"""Run a live Altair SITL session with desktop or browser visualization."""

from __future__ import annotations

import argparse
import os
import pathlib
import shlex
import shutil
import signal
import subprocess
import sys
import time


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def endpoint(text: str) -> tuple[str, int]:
    if ":" not in text:
        raise argparse.ArgumentTypeError(f"expected host:port, got {text!r}")
    host, port_text = text.rsplit(":", 1)
    try:
        port = int(port_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid endpoint port: {text!r}") from exc
    if port <= 0 or port > 65535:
        raise argparse.ArgumentTypeError(f"endpoint port out of range: {text!r}")
    return host, port


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(
        description="Run realtime cruise6dof SITL through the MAVLink live bridge."
    )
    parser.add_argument("--build-dir", default=str(root / "build"))
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=("cruise", "takeoff", "turn", "descent", "failsafe", "mission"),
    )
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", default="sitl_live.csv", help="CSV log path")
    parser.add_argument(
        "--initial",
        help="initial-condition file; defaults to the repository cruise6dof fixture",
    )
    parser.add_argument("--case", help="sectioned SITL case file")
    parser.add_argument("--conditions", help="per-step SITL condition file")
    parser.add_argument(
        "--frame-mode", choices=("ned", "ecef"), default="ecef", help="6DOF truth frame"
    )
    parser.add_argument("--bridge-host", default="127.0.0.1", help="bridge UDP listen host")
    parser.add_argument("--bridge-port", type=int, default=14551, help="bridge UDP listen port")
    parser.add_argument("--vehicles", type=int, default=1, help="number of SITL vehicle instances to launch")
    parser.add_argument("--system-id-base", type=int, default=1, help="first MAVLink system id for swarm launches")
    parser.add_argument("--mavlink-port-base", type=int, default=14600, help="first per-vehicle MAVLink UDP source port for swarm launches")
    parser.add_argument("--ws-host", default="127.0.0.1", help="viewer WebSocket host")
    parser.add_argument("--ws-port", type=int, default=8765, help="viewer WebSocket port")
    parser.add_argument(
        "--qgc",
        dest="qgc",
        action="store_true",
        default=True,
        help="forward MAVLink packets to QGroundControl",
    )
    parser.add_argument("--no-qgc", dest="qgc", action="store_false")
    parser.add_argument(
        "--qgc-endpoint",
        action="append",
        type=endpoint,
        default=[],
        help="QGC MAVLink UDP endpoint as host:port; repeatable",
    )
    parser.add_argument(
        "--viewer",
        dest="viewer",
        action="store_true",
        default=True,
        help="start the browser Animus dev server",
    )
    parser.add_argument("--no-viewer", dest="viewer", action="store_false")
    parser.add_argument(
        "--app",
        action="store_true",
        help="start the packaged Electron Animus instead of the browser viewer and Python bridge",
    )
    parser.add_argument(
        "--writable-animus",
        action="store_true",
        help="enable guarded SITL-only write controls in Animus; disabled by default",
    )
    parser.add_argument("--viewer-host", default="127.0.0.1")
    parser.add_argument("--viewer-port", type=int, default=5173)
    parser.add_argument(
        "--install-viewer-deps",
        action="store_true",
        help="run npm install in tools/animus before starting the viewer",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without starting any processes",
    )
    args = parser.parse_args(argv)
    if args.initial is None and args.case is None:
        args.initial = str(root / "tests" / "integration" / "cruise6dof_initial.ini")
    if not args.qgc and args.qgc_endpoint:
        parser.error("--qgc-endpoint requires --qgc")
    if args.app and not args.viewer:
        parser.error("--app cannot be combined with --no-viewer")
    if args.vehicles < 1:
        parser.error("--vehicles must be at least 1")
    if args.system_id_base < 1 or args.system_id_base + args.vehicles - 1 > 255:
        parser.error("--system-id-base plus --vehicles must fit MAVLink system ids 1..255")
    if args.mavlink_port_base < 1 or args.mavlink_port_base + args.vehicles - 1 > 65535:
        parser.error("--mavlink-port-base plus --vehicles must fit UDP ports 1..65535")
    return args


def quote_command(command: list[str]) -> str:
    return shlex.join(str(part) for part in command)


def bridge_command(args: argparse.Namespace) -> list[str]:
    root = repo_root()
    command = [
        sys.executable,
        str(root / "tools" / "python" / "mavlink_live_bridge.py"),
        "--listen-host",
        args.bridge_host,
        "--listen-port",
        str(args.bridge_port),
        "--ws-host",
        args.ws_host,
        "--ws-port",
        str(args.ws_port),
    ]
    if args.qgc:
        endpoints = args.qgc_endpoint or [("127.0.0.1", 14550)]
        for host, port in endpoints:
            command.extend(["--forward", f"{host}:{port}"])
    else:
        command.append("--no-forward")
    if args.writable_animus:
        command.append("--writable-animus")
    return command


def viewer_install_command() -> list[str]:
    return ["npm", "install"]


def viewer_command(args: argparse.Namespace) -> list[str]:
    return [
        "npm",
        "run",
        "dev",
        "--",
        "--host",
        args.viewer_host,
        "--port",
        str(args.viewer_port),
    ]


def app_command(args: argparse.Namespace) -> list[str]:
    command = [
        "npm",
        "run",
        "app",
        "--",
        "--listen-host",
        args.bridge_host,
        "--listen-port",
        str(args.bridge_port),
    ]
    if args.qgc:
        for host, port in args.qgc_endpoint or [("127.0.0.1", 14550)]:
            command.extend(["--qgc-endpoint", f"{host}:{port}"])
    else:
        command.append("--no-qgc")
    if args.writable_animus:
        command.append("--writable-animus")
    return command


def sitl_command(args: argparse.Namespace, vehicle_index: int = 0) -> list[str]:
    root = repo_root()
    system_id = args.system_id_base + vehicle_index
    mavlink_source_port = args.mavlink_port_base + vehicle_index
    output = args.output if args.vehicles == 1 else swarm_output_path(args.output, system_id)
    command = [
        sys.executable,
        str(root / "tools" / "python" / "run_sitl.py"),
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
        output,
        "--frame-mode",
        args.frame_mode,
        "--realtime",
        "--mavlink",
        "--mavlink-host",
        args.bridge_host,
        "--mavlink-port",
        str(args.bridge_port),
        "--mavlink-system-id",
        str(system_id),
        "--mavlink-source-port",
        str(mavlink_source_port),
    ]
    if args.initial:
        command.extend(["--initial", args.initial])
    if args.case:
        command.extend(["--case", args.case])
    if args.conditions:
        command.extend(["--conditions", args.conditions])
    return command


def swarm_output_path(output: str, system_id: int) -> str:
    path = pathlib.Path(output)
    suffix = "".join(path.suffixes)
    stem = path.name[: -len(suffix)] if suffix else path.name
    name = f"{stem}_sys{system_id}{suffix}"
    return str(path.with_name(name))


def require_viewer_ready(root: pathlib.Path, install_deps: bool, dry_run: bool) -> None:
    viewer_dir = root / "tools" / "animus"
    if dry_run:
        return
    if shutil.which("npm") is None:
        raise RuntimeError("npm is required to start Animus")
    if install_deps:
        return
    if not (viewer_dir / "node_modules").exists():
        raise RuntimeError(
            "tools/animus/node_modules is missing; rerun with --install-viewer-deps"
        )


def terminate(processes: list[subprocess.Popen]) -> None:
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
    deadline = time.monotonic() + 5.0
    for process in reversed(processes):
        remaining = max(0.0, deadline - time.monotonic())
        try:
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def run(args: argparse.Namespace) -> int:
    root = repo_root()
    viewer_dir = root / "tools" / "animus"
    commands: list[tuple[str, list[str], pathlib.Path | None]] = []
    if args.app:
        require_viewer_ready(root, args.install_viewer_deps, args.dry_run)
        if args.install_viewer_deps:
            commands.append(("viewer dependencies", viewer_install_command(), viewer_dir))
        commands.append(("app", app_command(args), viewer_dir))
    else:
        commands.append(("bridge", bridge_command(args), None))
    if args.viewer and not args.app:
        require_viewer_ready(root, args.install_viewer_deps, args.dry_run)
        if args.install_viewer_deps:
            commands.append(("viewer dependencies", viewer_install_command(), viewer_dir))
        commands.append(("viewer", viewer_command(args), viewer_dir))
    for index in range(args.vehicles):
        label = "sitl" if args.vehicles == 1 else f"sitl sys{args.system_id_base + index}"
        commands.append((label, sitl_command(args, index), None))

    if args.dry_run:
        for label, command, cwd in commands:
            prefix = f"(cd {cwd} && " if cwd is not None else ""
            suffix = ")" if cwd is not None else ""
            print(f"{label}: {prefix}{quote_command(command)}{suffix}")
        return 0

    processes: list[subprocess.Popen] = []
    previous_sigterm = signal.getsignal(signal.SIGTERM)

    def stop(_signum, _frame) -> None:
        terminate(processes)

    signal.signal(signal.SIGTERM, stop)
    try:
        print(
            (
                "viewer=app"
                if args.app
                else f"viewer=http://{args.viewer_host}:{args.viewer_port}"
                if args.viewer
                else "viewer=disabled"
            ),
            flush=True,
        )
        if args.qgc:
            qgc_targets = args.qgc_endpoint or [("127.0.0.1", 14550)]
            print(f"qgc={', '.join(f'{host}:{port}' for host, port in qgc_targets)}", flush=True)
        else:
            print("qgc=disabled", flush=True)
        for label, command, cwd in commands[:-1]:
            print(f"\n== Start {label} ==", flush=True)
            print(quote_command(command), flush=True)
            processes.append(subprocess.Popen(command, cwd=cwd, text=True))
            time.sleep(0.3)
            if processes[-1].poll() is not None:
                return processes[-1].returncode or 1

        if args.vehicles == 1:
            label, command, cwd = commands[-1]
            print(f"\n== Run {label} ==", flush=True)
            print(quote_command(command), flush=True)
            sitl = subprocess.Popen(command, cwd=cwd, text=True)
            processes.append(sitl)
            return sitl.wait()
        print(f"\n== Run swarm ({args.vehicles} vehicles) ==", flush=True)
        for label, command, cwd in commands[-args.vehicles :]:
            print(f"{label}: {quote_command(command)}", flush=True)
            processes.append(subprocess.Popen(command, cwd=cwd, text=True))
            time.sleep(0.2)
        return max(process.wait() for process in processes[-args.vehicles :])
    except KeyboardInterrupt:
        return 130
    except (OSError, RuntimeError) as exc:
        print(f"run_sitl_session.py: {exc}", file=sys.stderr)
        return 1
    finally:
        terminate(processes)
        signal.signal(signal.SIGTERM, previous_sigterm)


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

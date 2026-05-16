#!/usr/bin/env python3
"""Capture Animus screenshots from a live SITL session."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import signal
import socket
import subprocess
import sys
import time
from datetime import datetime, timezone


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run Animus against live SITL and capture Electron screenshots."
    )
    parser.add_argument("--duration", type=float, default=12.0, help="SITL duration in seconds")
    parser.add_argument("--warmup", type=float, default=4.0, help="seconds before capture starts")
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=("cruise", "takeoff", "turn", "descent", "failsafe", "mission"),
    )
    parser.add_argument("--build-dir", default=str(repo_root() / "build"))
    parser.add_argument(
        "--output-dir",
        help="artifact directory; defaults to artifacts/animus-screenshots/<timestamp>",
    )
    parser.add_argument(
        "--workspace", action="append", dest="workspaces", help="workspace to capture; repeatable"
    )
    parser.add_argument(
        "--viewport", action="append", dest="viewports", help="viewport WIDTHxHEIGHT; repeatable"
    )
    parser.add_argument(
        "--install-viewer-deps", action="store_true", help="run npm install in tools/animus first"
    )
    parser.add_argument(
        "--keep-going", action="store_true", help="write manifest even when capture fails"
    )
    args = parser.parse_args(argv)
    if args.duration <= 0:
        parser.error("--duration must be positive")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    args.workspaces = args.workspaces or ["flight", "map", "inspector", "plan", "setup", "video"]
    args.viewports = args.viewports or ["1440x900"]
    return args


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_tcp(host: str, port: int, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"timed out waiting for {host}:{port}")


def quote_command(command: list[str]) -> str:
    return " ".join(subprocess.list2cmdline([part]) for part in command)


def popen_logged(
    command: list[str],
    *,
    cwd: pathlib.Path | None,
    log_path: pathlib.Path,
) -> tuple[subprocess.Popen, object]:
    log_file = log_path.open("w", encoding="utf8")
    print(f"start={quote_command(command)}")
    process = subprocess.Popen(
        command,
        cwd=cwd,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return process, log_file


def terminate(processes: list[subprocess.Popen]) -> None:
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
    deadline = time.monotonic() + 5.0
    for process in reversed(processes):
        if process.poll() is not None:
            continue
        try:
            process.wait(timeout=max(0.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def require_tools(root: pathlib.Path, install_deps: bool) -> None:
    if shutil.which("npm") is None:
        raise RuntimeError("npm is required for Animus capture")
    animus_dir = root / "tools" / "animus"
    if not install_deps and not (animus_dir / "node_modules").exists():
        raise RuntimeError("tools/animus/node_modules is missing; rerun with --install-viewer-deps")


def electron_capture_command(command: list[str]) -> list[str]:
    if os.environ.get("DISPLAY"):
        return command
    xvfb_run = shutil.which("xvfb-run")
    if xvfb_run is None:
        raise RuntimeError("Electron capture requires DISPLAY or xvfb-run")
    return [xvfb_run, "-a", *command]


def run(args: argparse.Namespace) -> int:
    root = repo_root()
    animus_dir = root / "tools" / "animus"
    out_dir = (
        pathlib.Path(args.output_dir)
        if args.output_dir
        else root / "artifacts" / "animus-screenshots" / timestamp()
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    processes: list[subprocess.Popen] = []
    log_files: list[object] = []
    previous_sigterm = signal.getsignal(signal.SIGTERM)

    def stop(_signum, _frame) -> None:
        terminate(processes)

    signal.signal(signal.SIGTERM, stop)
    manifest = {
        "artifactDir": str(out_dir),
        "liveTelemetry": False,
        "screenshots": [],
        "logs": {},
        "errors": [],
    }

    try:
        require_tools(root, args.install_viewer_deps)
        bridge_port = free_udp_port()
        ws_port = free_tcp_port()
        viewer_port = free_tcp_port()
        mavlink_source_port = free_udp_port()

        if args.install_viewer_deps:
            subprocess.run(["npm", "install"], cwd=animus_dir, check=True)

        commands = {
            "bridge": [
                sys.executable,
                str(root / "tools" / "python" / "mavlink_live_bridge.py"),
                "--listen-host",
                "127.0.0.1",
                "--listen-port",
                str(bridge_port),
                "--ws-host",
                "127.0.0.1",
                "--ws-port",
                str(ws_port),
                "--no-forward",
            ],
            "viewer": [
                "npm",
                "run",
                "dev",
                "--",
                "--host",
                "127.0.0.1",
                "--port",
                str(viewer_port),
            ],
            "sitl": [
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
                "0.01",
                "--seed",
                "1",
                "--output",
                str(out_dir / "sitl_live.csv"),
                "--frame-mode",
                "ecef",
                "--realtime",
                "--mavlink",
                "--mavlink-host",
                "127.0.0.1",
                "--mavlink-port",
                str(bridge_port),
                "--mavlink-source-port",
                str(mavlink_source_port),
                "--initial",
                str(root / "tests" / "integration" / "cruise6dof_initial.ini"),
            ],
        }

        for label in ("bridge", "viewer", "sitl"):
            log_path = out_dir / f"{label}.log"
            cwd = animus_dir if label == "viewer" else root
            process, log_file = popen_logged(commands[label], cwd=cwd, log_path=log_path)
            processes.append(process)
            log_files.append(log_file)
            manifest["logs"][label] = str(log_path)
            time.sleep(0.3)
            if process.poll() is not None:
                raise RuntimeError(f"{label} exited early with code {process.returncode}")

        wait_for_tcp("127.0.0.1", viewer_port, 20.0)
        time.sleep(args.warmup)

        url = f"http://127.0.0.1:{viewer_port}/?ws=ws://127.0.0.1:{ws_port}"
        capture_command = electron_capture_command(
            [
                "npm",
                "run",
                "capture",
                "--",
                "--url",
                url,
                "--out-dir",
                str(out_dir),
                "--workspaces",
                ",".join(args.workspaces),
                "--viewports",
                ",".join(args.viewports),
                "--wait-timeout-ms",
                str(max(5000, int((args.warmup + 4.0) * 1000))),
            ]
        )
        capture_log = out_dir / "capture.log"
        manifest["logs"]["capture"] = str(capture_log)
        with capture_log.open("w", encoding="utf8") as log_file:
            result = subprocess.run(
                capture_command,
                cwd=animus_dir,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                text=True,
            )
        if result.returncode != 0:
            raise RuntimeError(f"capture failed with code {result.returncode}; see {capture_log}")

        capture_manifest_path = out_dir / "manifest.json"
        capture_manifest = json.loads(capture_manifest_path.read_text(encoding="utf8"))
        manifest["liveTelemetry"] = bool(capture_manifest.get("liveTelemetry"))
        manifest["screenshots"] = [item["path"] for item in capture_manifest.get("captures", [])]
        manifest["captureManifest"] = str(capture_manifest_path)
        return 0
    except Exception as exc:
        manifest["errors"].append(str(exc))
        if not args.keep_going:
            print(f"capture_animus_sitl.py: {exc}", file=sys.stderr)
            return 1
        return 0
    finally:
        terminate(processes)
        for log_file in log_files:
            log_file.close()
        (out_dir / "run-manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf8"
        )
        print(f"artifactDir={out_dir}")
        print(f"liveTelemetry={manifest['liveTelemetry']}")
        for path in manifest["screenshots"]:
            print(f"screenshot={path}")
        if manifest["errors"]:
            print(f"errors={'; '.join(manifest['errors'])}")
        signal.signal(signal.SIGTERM, previous_sigterm)


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Run a full live-SITL visual audit of Animus across viewport classes."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

from interact_animus_sitl import (
    collect_screenshots,
    free_tcp_port,
    free_udp_port,
    popen_logged,
    playwright_command,
    quote_command,
    repo_root,
    require_tools,
    terminate,
    wait_for_tcp,
)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the full Animus Playwright audit against live SITL."
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=3600.0,
        help="SITL duration in seconds; must cover all viewport audit passes",
    )
    parser.add_argument("--warmup", type=float, default=4.0, help="seconds before audit starts")
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=("cruise", "takeoff", "turn", "descent", "failsafe", "mission"),
    )
    parser.add_argument("--build-dir", default=str(repo_root() / "build"))
    parser.add_argument(
        "--viewport",
        action="append",
        dest="viewports",
        help="Playwright viewport WIDTHxHEIGHT; repeatable",
    )
    parser.add_argument(
        "--output-dir",
        help="artifact directory; defaults to artifacts/animus-interactions/<timestamp>-audit",
    )
    parser.add_argument("--headed", action="store_true", help="run Chromium headed")
    parser.add_argument(
        "--install-viewer-deps",
        action="store_true",
        help="run npm install and install Playwright Chromium in tools/animus first",
    )
    parser.add_argument(
        "--keep-going", action="store_true", help="write manifest and continue after failures"
    )
    args = parser.parse_args(argv)
    if args.duration <= 0:
        parser.error("--duration must be positive")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    args.viewports = args.viewports or ["1440x900", "1024x768", "390x844"]
    for viewport in args.viewports:
        if "x" not in viewport:
            parser.error("--viewport must use WIDTHxHEIGHT")
    return args


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def run(args: argparse.Namespace) -> int:
    root = repo_root()
    animus_dir = root / "tools" / "animus"
    out_dir = (
        pathlib.Path(args.output_dir)
        if args.output_dir
        else root / "artifacts" / "animus-interactions" / f"{timestamp()}-audit"
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "screenshots").mkdir(parents=True, exist_ok=True)

    processes: list[subprocess.Popen] = []
    log_files: list[object] = []
    previous_sigterm = signal.getsignal(signal.SIGTERM)

    def stop(_signum, _frame) -> None:
        terminate(processes)

    signal.signal(signal.SIGTERM, stop)
    manifest = {
        "artifactDir": str(out_dir),
        "baseUrl": None,
        "liveTelemetry": False,
        "viewports": args.viewports,
        "screenshots": [],
        "logs": {},
        "ports": {},
        "commands": [],
        "errors": [],
    }

    try:
        require_tools(root, args.install_viewer_deps)
        bridge_port = free_udp_port()
        ws_port = free_tcp_port()
        viewer_port = free_tcp_port()
        mavlink_source_port = free_udp_port()
        manifest["ports"] = {
            "bridgeUdp": bridge_port,
            "websocket": ws_port,
            "viewer": viewer_port,
            "mavlinkSourceUdp": mavlink_source_port,
        }

        if args.install_viewer_deps:
            subprocess.run(["npm", "install"], cwd=animus_dir, check=True)
            subprocess.run(["npx", "playwright", "install", "chromium"], cwd=animus_dir, check=True)

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
            manifest["commands"].append(quote_command(commands[label]))
            time.sleep(0.3)
            if process.poll() is not None:
                raise RuntimeError(f"{label} exited early with code {process.returncode}")

        wait_for_tcp("127.0.0.1", viewer_port, 20.0)
        time.sleep(args.warmup)

        url = f"http://127.0.0.1:{viewer_port}/?ws=ws://127.0.0.1:{ws_port}"
        manifest["baseUrl"] = url
        for viewport in args.viewports:
            command = playwright_command(
                [
                    "npm",
                    "run",
                    "test:e2e",
                    "--",
                    "tests/e2e/animus-audit.spec.ts",
                    *(["--headed"] if args.headed else []),
                ],
                args.headed,
            )
            log_path = out_dir / f"playwright-audit-{viewport}.log"
            manifest["logs"][f"playwright-{viewport}"] = str(log_path)
            manifest["commands"].append(f"ANIMUS_VIEWPORT={viewport} {quote_command(command)}")
            env = {
                **os.environ,
                "ANIMUS_BASE_URL": url,
                "ANIMUS_ARTIFACT_DIR": str(out_dir),
                "ANIMUS_VIEWPORT": viewport,
                "ANIMUS_AUDIT_VIEWPORT": viewport,
                "ANIMUS_FULL_AUDIT": "1",
            }
            with log_path.open("w", encoding="utf8") as log_file:
                result = subprocess.run(
                    command,
                    cwd=animus_dir,
                    env=env,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                    text=True,
                )
            if result.returncode != 0:
                message = f"Playwright audit failed for {viewport} with code {result.returncode}; see {log_path}"
                manifest["errors"].append(message)
                if not args.keep_going:
                    raise RuntimeError(message)

        manifest["screenshots"] = collect_screenshots(out_dir)
        manifest["liveTelemetry"] = len(manifest["screenshots"]) > 0
        return 0 if not manifest["errors"] or args.keep_going else 1
    except Exception as exc:
        if str(exc) not in manifest["errors"]:
            manifest["errors"].append(str(exc))
        if not args.keep_going:
            print(f"audit_animus_sitl.py: {exc}", file=sys.stderr)
            return 1
        return 0
    finally:
        terminate(processes)
        for log_file in log_files:
            log_file.close()
        manifest["screenshots"] = collect_screenshots(out_dir)
        manifest["liveTelemetry"] = manifest["liveTelemetry"] or len(manifest["screenshots"]) > 0
        (out_dir / "run-manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf8"
        )
        print(f"artifactDir={out_dir}")
        print(f"liveTelemetry={manifest['liveTelemetry']}")
        print(f"screenshotCount={len(manifest['screenshots'])}")
        if manifest["errors"]:
            print(f"errors={'; '.join(manifest['errors'])}")
        signal.signal(signal.SIGTERM, previous_sigterm)


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

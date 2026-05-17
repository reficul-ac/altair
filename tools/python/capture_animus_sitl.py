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
from typing import Any


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
    parser.add_argument(
        "--capture-timeout",
        type=float,
        default=60.0,
        help="maximum seconds to wait for the Electron screenshot capture step",
    )
    parser.add_argument(
        "--settle-ms",
        type=int,
        default=800,
        help="milliseconds to wait after selecting each workspace before screenshot capture",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="write structured Electron capture diagnostics to capture.log",
    )
    parser.add_argument(
        "--fullscreen",
        action="store_true",
        help="capture after Electron enters true fullscreen and assert fullscreen layout diagnostics",
    )
    args = parser.parse_args(argv)
    if args.duration <= 0:
        parser.error("--duration must be positive")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    if args.capture_timeout <= 0:
        parser.error("--capture-timeout must be positive")
    if args.settle_ms < 0:
        parser.error("--settle-ms must be non-negative")
    args.workspaces = args.workspaces or [
        "flight",
        "dashboard",
        "map",
        "inspector",
        "plan",
        "setup",
        "video",
    ]
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


def run_logged_with_timeout(
    command: list[str],
    *,
    cwd: pathlib.Path | None,
    log_path: pathlib.Path,
    timeout_s: float,
) -> int:
    with log_path.open("w", encoding="utf8") as log_file:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        try:
            return process.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            print(
                f"capture timed out after {timeout_s:.1f}s; terminating process group",
                file=log_file,
                flush=True,
            )
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            raise RuntimeError(f"capture timed out after {timeout_s:.1f}s; see {log_path}")


def markdown_cell(value: object) -> str:
    text = "" if value is None else str(value)
    return text.replace("\n", " ").replace("|", "\\|")


def markdown_path(path: object, artifact_dir: pathlib.Path) -> str:
    if isinstance(path, pathlib.Path):
        path_obj = path
    elif isinstance(path, str) and path:
        path_obj = pathlib.Path(path)
    else:
        return "-"
    try:
        label = path_obj.relative_to(artifact_dir)
    except ValueError:
        label = path_obj
    return f"`{label}`"


def format_bool(value: object) -> str:
    if isinstance(value, bool):
        return "yes" if value else "no"
    return "-"


def format_size(value: object) -> str:
    if isinstance(value, list) and len(value) >= 2:
        return f"{value[0]}x{value[1]}"
    if isinstance(value, tuple) and len(value) >= 2:
        return f"{value[0]}x{value[1]}"
    return "-"


def format_scene_size(diagnostics: dict[str, Any]) -> str:
    scene = diagnostics.get("sceneClient")
    if not isinstance(scene, dict):
        return "-"
    client_width = scene.get("width")
    client_height = scene.get("height")
    canvas_width = scene.get("canvasWidth")
    canvas_height = scene.get("canvasHeight")
    if client_width is None or client_height is None:
        return "-"
    if canvas_width is None or canvas_height is None:
        return f"{client_width}x{client_height}"
    return f"{client_width}x{client_height} / canvas {canvas_width}x{canvas_height}"


def scene_has_zero_dimension(diagnostics: dict[str, Any]) -> bool:
    scene = diagnostics.get("sceneClient")
    if not isinstance(scene, dict):
        return True
    keys = ("width", "height", "canvasWidth", "canvasHeight")
    values = [scene.get(key) for key in keys]
    return any(not isinstance(value, (int, float)) or value <= 0 for value in values)


def capture_warnings(
    capture: dict[str, Any],
    requested_workspace: str,
) -> list[str]:
    warnings: list[str] = []
    path = capture.get("path")
    if not isinstance(path, str) or not pathlib.Path(path).exists():
        warnings.append("missing screenshot")
    if not bool(capture.get("liveTelemetry")):
        warnings.append("no live telemetry")

    diagnostics = capture.get("diagnostics")
    if not isinstance(diagnostics, dict):
        warnings.append("missing diagnostics")
        return warnings

    active_workspace = diagnostics.get("activeWorkspace")
    if active_workspace != requested_workspace:
        warnings.append("active workspace mismatch")
    visible_panel = diagnostics.get("visiblePanel")
    if visible_panel != requested_workspace:
        warnings.append("visible panel mismatch")
    if requested_workspace == "flight" and scene_has_zero_dimension(diagnostics):
        warnings.append("flight scene/canvas dimensions missing or zero")
    if diagnostics.get("isCrashed"):
        warnings.append("renderer crashed")
    if diagnostics.get("isLoading"):
        warnings.append("renderer still loading")
    if diagnostics.get("rendererSnapshotError"):
        warnings.append("renderer snapshot error")

    return warnings


def read_capture_manifest(path: pathlib.Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    try:
        parsed = json.loads(path.read_text(encoding="utf8"))
    except json.JSONDecodeError:
        return None
    return parsed if isinstance(parsed, dict) else None


def generate_visual_report(
    *,
    out_dir: pathlib.Path,
    run_manifest: dict[str, Any],
    requested_workspaces: list[str],
    requested_viewports: list[str],
) -> pathlib.Path:
    capture_manifest_path = out_dir / "manifest.json"
    capture_manifest = read_capture_manifest(capture_manifest_path)
    captures_raw = capture_manifest.get("captures", []) if capture_manifest else []
    captures = [item for item in captures_raw if isinstance(item, dict)]
    capture_by_key = {
        (str(item.get("workspace")), str(item.get("viewport"))): item for item in captures
    }

    report_warnings: list[str] = []
    rows: list[list[str]] = []
    for viewport in requested_viewports:
        for workspace in requested_workspaces:
            capture = capture_by_key.get((workspace, viewport))
            if capture is None:
                report_warnings.append(f"missing screenshot entry for {workspace} {viewport}")
                rows.append(
                    [workspace, viewport, "-", "-", "-", "-", "-", "-", "missing screenshot entry"]
                )
                continue

            diagnostics_raw = capture.get("diagnostics")
            diagnostics = diagnostics_raw if isinstance(diagnostics_raw, dict) else {}
            warnings = capture_warnings(capture, workspace)
            report_warnings.extend(f"{workspace} {viewport}: {warning}" for warning in warnings)
            rows.append(
                [
                    workspace,
                    viewport,
                    markdown_path(capture.get("path"), out_dir),
                    format_size(diagnostics.get("contentSize")),
                    str(diagnostics.get("activeWorkspace") or "-"),
                    str(diagnostics.get("visiblePanel") or "-"),
                    format_scene_size(diagnostics),
                    str(diagnostics.get("vehicleId") or diagnostics.get("statusText") or "-"),
                    "pass" if not warnings else "; ".join(warnings),
                ]
            )

    if not bool(run_manifest.get("liveTelemetry")):
        report_warnings.append("no live telemetry")
    for error in run_manifest.get("errors", []):
        report_warnings.append(f"capture error: {error}")

    status = "PASS" if not report_warnings else "WARN"
    capture_status = "failed" if run_manifest.get("errors") else "completed"
    capture_manifest_live = capture_manifest.get("liveTelemetry") if capture_manifest else None

    logs = run_manifest.get("logs", {})
    if not isinstance(logs, dict):
        logs = {}

    lines = [
        "# Animus Visual Verification Report",
        "",
        "## Summary",
        "",
        f"- Status: **{status}**",
        f"- Capture status: {capture_status}",
        f"- Live telemetry: {format_bool(run_manifest.get('liveTelemetry'))}",
        f"- Electron live telemetry: {format_bool(capture_manifest_live)}",
        f"- Workspaces requested: {len(requested_workspaces)} ({', '.join(requested_workspaces)})",
        f"- Viewports requested: {', '.join(requested_viewports)}",
        f"- Screenshots captured: {len(captures)}",
        "",
        "## Warnings",
        "",
    ]
    if report_warnings:
        lines.extend(f"- {warning}" for warning in sorted(set(report_warnings)))
    else:
        lines.append("- None")

    lines.extend(
        [
            "",
            "## Screenshots",
            "",
            "| Workspace | Viewport | PNG | Content size | Active workspace | Visible panel | Scene/canvas | Vehicle/status | Notes |",
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- |",
        ]
    )
    for row in rows:
        lines.append("| " + " | ".join(markdown_cell(value) for value in row) + " |")

    lines.extend(
        [
            "",
            "## Artifacts",
            "",
            f"- Run manifest: {markdown_path(out_dir / 'run-manifest.json', out_dir)}",
            f"- Electron manifest: {markdown_path(capture_manifest_path, out_dir)}",
            f"- Capture log: {markdown_path(logs.get('capture'), out_dir)}",
            f"- Viewer log: {markdown_path(logs.get('viewer'), out_dir)}",
            f"- Bridge log: {markdown_path(logs.get('bridge'), out_dir)}",
            f"- SITL log: {markdown_path(logs.get('sitl'), out_dir)}",
            "",
        ]
    )

    report_path = out_dir / "visual-report.md"
    report_path.write_text("\n".join(lines), encoding="utf8")
    return report_path


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
                "--capture-timeout-ms",
                str(int(args.capture_timeout * 1000)),
                "--settle-ms",
                str(args.settle_ms),
            ]
        )
        if args.debug:
            capture_command.append("--debug")
        if args.fullscreen:
            capture_command.append("--fullscreen")
        capture_log = out_dir / "capture.log"
        manifest["logs"]["capture"] = str(capture_log)
        returncode = run_logged_with_timeout(
            capture_command,
            cwd=animus_dir,
            log_path=capture_log,
            timeout_s=args.capture_timeout,
        )
        if returncode != 0:
            raise RuntimeError(f"capture failed with code {returncode}; see {capture_log}")

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
        run_manifest_path = out_dir / "run-manifest.json"
        run_manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
        try:
            report_viewports = [
                f"fullscreen-{viewport}" if args.fullscreen else viewport
                for viewport in args.viewports
            ]
            report_path = generate_visual_report(
                out_dir=out_dir,
                run_manifest=manifest,
                requested_workspaces=args.workspaces,
                requested_viewports=report_viewports,
            )
            manifest["visualReport"] = str(report_path)
            run_manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
        except Exception as exc:
            print(f"visual report generation failed: {exc}", file=sys.stderr)
        print(f"artifactDir={out_dir}")
        print(f"liveTelemetry={manifest['liveTelemetry']}")
        if manifest.get("visualReport"):
            print(f"visualReport={manifest['visualReport']}")
        for path in manifest["screenshots"]:
            print(f"screenshot={path}")
        if manifest["errors"]:
            print(f"errors={'; '.join(manifest['errors'])}")
        signal.signal(signal.SIGTERM, previous_sigterm)


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

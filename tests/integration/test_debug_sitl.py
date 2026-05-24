#!/usr/bin/env python3

import json
import shlex
import subprocess
import sys
from pathlib import Path


def run_debug_sitl(repo_root, build_dir, *args, dry_run=True):
    command = [
        sys.executable,
        str(repo_root / "tools/python/debug_sitl.py"),
        "--build-dir",
        str(build_dir),
    ]
    if dry_run:
        command.append("--dry-run")
    command.extend(args)
    return subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def require_success(result):
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise AssertionError(f"debug_sitl.py failed with {result.returncode}")


def main():
    if len(sys.argv) != 3:
        print("usage: test_debug_sitl.py <repo-root> <build-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    build_dir = Path(sys.argv[2])
    sitl_runner = build_dir / "vehicle/sitl_runner"
    if not sitl_runner.exists():
        print(f"skipping debug_sitl checks: {sitl_runner} is missing", file=sys.stderr)
        return 77

    result = run_debug_sitl(repo_root, build_dir)
    require_success(result)
    default_command = shlex.split(result.stdout)
    require(default_command[0] == "gdb", "default debugger was not gdb")
    require(str(sitl_runner) in default_command, "sitl_runner path missing from default command")
    require("--scenario" in default_command, "--scenario missing from default command")
    require("cruise6dof" in default_command, "default scenario was not cruise6dof")
    require("break run_cruise6dof" in default_command, "default run_cruise6dof breakpoint missing")

    result = run_debug_sitl(repo_root, build_dir, "--break", "main", "--break", "altair_fsw_step")
    require_success(result)
    command = shlex.split(result.stdout)
    require("break main" in command, "first explicit breakpoint missing")
    require("break altair_fsw_step" in command, "second explicit breakpoint missing")
    require("break run_cruise6dof" not in command, "default breakpoint should not be added")

    result = run_debug_sitl(repo_root, build_dir, "--stop-at-step", "50")
    require_success(result)
    command = shlex.split(result.stdout)
    require(
        "break sitl_debug_pre_fsw_step_hook if step == 50" in command,
        "step conditional hook breakpoint missing",
    )

    result = run_debug_sitl(repo_root, build_dir, "--stop-at-time", "0.5")
    require_success(result)
    command = shlex.split(result.stdout)
    require(
        "break sitl_debug_pre_fsw_step_hook if time_s >= 0.5" in command,
        "time conditional hook breakpoint missing",
    )

    result = run_debug_sitl(
        repo_root,
        build_dir,
        "--debugger",
        "lldb",
        "--break",
        "main",
        "--stop-at-step",
        "50",
    )
    require_success(result)
    command = shlex.split(result.stdout)
    require(command[0] == "lldb", "lldb command did not start with lldb")
    require("breakpoint set --name main" in command, "lldb symbol breakpoint missing")
    require(
        "breakpoint set --name sitl_debug_pre_fsw_step_hook --condition 'step == 50'" in command,
        "lldb conditional hook breakpoint missing",
    )

    output = build_dir / "debug_sitl_passed.csv"
    result = run_debug_sitl(
        repo_root,
        build_dir,
        "--initial",
        str(repo_root / "tests/integration/cruise6dof_initial.ini"),
        "--duration",
        "1.25",
        "--dt",
        "0.02",
        "--output",
        str(output),
        "--frame-mode",
        "ned",
        "--break",
        "main",
    )
    require_success(result)
    command = shlex.split(result.stdout)
    for token in (
        "--initial",
        str(repo_root / "tests/integration/cruise6dof_initial.ini"),
        "--duration",
        "1.25",
        "--dt",
        "0.02",
        "--output",
        str(output),
        "--frame-mode",
        "ned",
    ):
        require(token in command, f"{token} missing from pass-through command")

    result = run_debug_sitl(repo_root, build_dir, "--emit-vscode-launch", dry_run=False)
    require_success(result)
    config = json.loads(result.stdout)
    require(config["name"] == "Debug cruise6dof SITL", "default VS Code launch name changed")
    require(config["type"] == "cppdbg", "VS Code config type was not cppdbg")
    require(config["request"] == "launch", "VS Code config request was not launch")
    require(config["program"] == str(sitl_runner), "VS Code program did not point at sitl_runner")
    require(config["cwd"] == "${workspaceFolder}", "VS Code cwd was not workspaceFolder")
    require(config["MIMode"] == "gdb", "default VS Code MIMode was not gdb")
    require(config["stopAtEntry"] is False, "VS Code stopAtEntry should be false")
    require(config["args"][0:2] == ["--scenario", "cruise6dof"], "default scenario args missing")
    for token in ("--profile", "cruise", "--duration", "5.0", "--dt", "0.01"):
        require(token in config["args"], f"{token} missing from default VS Code args")

    vscode_output = build_dir / "debug_sitl_vscode.csv"
    result = run_debug_sitl(
        repo_root,
        build_dir,
        "--emit-vscode-launch",
        "--initial",
        str(repo_root / "tests/integration/cruise6dof_initial.ini"),
        "--case",
        str(repo_root / "tests/integration/cruise6dof_case_mission.ini"),
        "--conditions",
        str(repo_root / "tests/integration/cruise6dof_conditions_gps.ini"),
        "--duration",
        "1.25",
        "--dt",
        "0.02",
        "--output",
        str(vscode_output),
        "--frame-mode",
        "ned",
        "--mavlink",
        "--mavlink-host",
        "127.0.0.2",
        "--mavlink-port",
        "14551",
        "--mavlink-system-id",
        "42",
        "--mavlink-source-port",
        "14552",
        dry_run=False,
    )
    require_success(result)
    config = json.loads(result.stdout)
    for token in (
        "--initial",
        str(repo_root / "tests/integration/cruise6dof_initial.ini"),
        "--case",
        str(repo_root / "tests/integration/cruise6dof_case_mission.ini"),
        "--conditions",
        str(repo_root / "tests/integration/cruise6dof_conditions_gps.ini"),
        "--duration",
        "1.25",
        "--dt",
        "0.02",
        "--output",
        str(vscode_output),
        "--frame-mode",
        "ned",
        "--mavlink",
        "--mavlink-host",
        "127.0.0.2",
        "--mavlink-port",
        "14551",
        "--mavlink-system-id",
        "42",
        "--mavlink-source-port",
        "14552",
    ):
        require(token in config["args"], f"{token} missing from VS Code pass-through args")

    result = run_debug_sitl(
        repo_root,
        build_dir,
        "--debugger",
        "lldb",
        "--emit-vscode-launch",
        dry_run=False,
    )
    require_success(result)
    config = json.loads(result.stdout)
    require(config["MIMode"] == "lldb", "VS Code MIMode did not default to lldb")

    result = run_debug_sitl(repo_root, build_dir, "--emit-vscode-launch")
    require(result.returncode != 0, "--dry-run and --emit-vscode-launch should fail together")
    require(
        "--dry-run and --emit-vscode-launch cannot be used together" in result.stderr,
        "combined dry-run/VS Code parser error missing",
    )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)

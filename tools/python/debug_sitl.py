#!/usr/bin/env python3
"""Launch sitl_runner under a native debugger with SITL-focused breakpoints."""

import argparse
import shlex
import subprocess
import sys

from sitl_runner_command import (
    add_sitl_runner_arguments,
    build_sitl_runner_command,
    sitl_runner_path,
    validate_sitl_runner_args,
)


HOOK_SYMBOL = "sitl_debug_pre_fsw_step_hook"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Debug Altair SITL with GDB or LLDB.")
    add_sitl_runner_arguments(parser, default_scenario="cruise6dof")
    parser.add_argument("--debugger", choices=("gdb", "lldb"), default="gdb")
    parser.add_argument(
        "--break",
        dest="breakpoints",
        action="append",
        default=[],
        metavar="SYMBOL_OR_FILE_LINE",
        help="add a debugger breakpoint; repeat for multiple breakpoints",
    )
    parser.add_argument(
        "--stop-at-step",
        type=int,
        help=f"break on {HOOK_SYMBOL} when the hook step argument equals N",
    )
    parser.add_argument(
        "--stop-at-time",
        type=float,
        help=f"break on {HOOK_SYMBOL} when the hook time_s argument reaches T",
    )
    parser.add_argument(
        "--condition",
        action="append",
        default=[],
        metavar="EXPR",
        help=f"add a conditional breakpoint on {HOOK_SYMBOL}; repeat for multiple expressions",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the debugger command")
    args = parser.parse_args()
    validate_sitl_runner_args(parser, args)
    if args.stop_at_step is not None and args.stop_at_step < 0:
        parser.error("--stop-at-step must be nonnegative")
    return args


def lldb_breakpoint_command(target: str) -> str:
    if ":" in target:
        path, line = target.rsplit(":", 1)
        if line.isdigit():
            return f"breakpoint set --file {shlex.quote(path)} --line {line}"
    return f"breakpoint set --name {target}"


def debugger_commands(args: argparse.Namespace) -> list[str]:
    breakpoints = list(args.breakpoints)
    if (
        not breakpoints
        and args.stop_at_step is None
        and args.stop_at_time is None
        and not args.condition
    ):
        breakpoints.append("run_cruise6dof")

    commands = []
    if args.debugger == "gdb":
        commands.extend(f"break {target}" for target in breakpoints)
        if args.stop_at_step is not None:
            commands.append(f"break {HOOK_SYMBOL} if step == {args.stop_at_step}")
        if args.stop_at_time is not None:
            commands.append(f"break {HOOK_SYMBOL} if time_s >= {args.stop_at_time}")
        commands.extend(f"break {HOOK_SYMBOL} if {condition}" for condition in args.condition)
    else:
        commands.extend(lldb_breakpoint_command(target) for target in breakpoints)
        if args.stop_at_step is not None:
            commands.append(
                f"breakpoint set --name {HOOK_SYMBOL} --condition 'step == {args.stop_at_step}'"
            )
        if args.stop_at_time is not None:
            commands.append(
                f"breakpoint set --name {HOOK_SYMBOL} --condition 'time_s >= {args.stop_at_time}'"
            )
        commands.extend(
            f"breakpoint set --name {HOOK_SYMBOL} --condition {shlex.quote(condition)}"
            for condition in args.condition
        )
    return commands


def debugger_command(args: argparse.Namespace) -> list[str]:
    sitl_command = build_sitl_runner_command(args)
    commands = debugger_commands(args)
    if args.debugger == "gdb":
        debugger = ["gdb"]
        for command in commands:
            debugger.extend(["-ex", command])
        debugger.append("--args")
    else:
        debugger = ["lldb"]
        for command in commands:
            debugger.extend(["-o", command])
        debugger.append("--")
    return debugger + sitl_command


def main() -> int:
    args = parse_args()
    exe = sitl_runner_path(args.build_dir)
    if not exe.exists():
        print(f"debug_sitl.py: missing {exe}", file=sys.stderr)
        print(
            "debug_sitl.py: build it with "
            "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && "
            "cmake --build build --target sitl_runner --parallel",
            file=sys.stderr,
        )
        return 1

    command = debugger_command(args)
    if args.dry_run:
        print(shlex.join(command))
        return 0
    try:
        return subprocess.call(command)
    except OSError as exc:
        print(f"debug_sitl.py: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

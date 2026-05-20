"""Shared command-line helpers for Altair's compiled SITL runner."""

import argparse
import pathlib
from typing import Union

SCENARIOS = ("smoke", "cruise6dof")
PROFILES = ("cruise", "takeoff", "turn", "descent", "failsafe", "mission")
FRAME_MODES = ("ned", "ecef")


def add_sitl_runner_arguments(
    parser: argparse.ArgumentParser,
    *,
    default_scenario: str = "smoke",
) -> None:
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--scenario", default=default_scenario, choices=SCENARIOS)
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=PROFILES,
        help="command profile for cruise6dof",
    )
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", default="sitl.csv")
    parser.add_argument("--initial", help="initial-condition file for cruise6dof")
    parser.add_argument("--case", help="sectioned SITL case file for cruise6dof")
    parser.add_argument("--conditions", help="per-step SITL condition file for cruise6dof")
    parser.add_argument(
        "--frame-mode", choices=FRAME_MODES, default="ecef", help="6DOF truth frame"
    )
    parser.add_argument(
        "--realtime",
        action="store_true",
        help="pace the compiled runner so one simulated second takes one wall-clock second",
    )
    parser.add_argument(
        "--qgc",
        "--mavlink",
        dest="qgc",
        action="store_true",
        help="stream cruise6dof SITL telemetry over MAVLink UDP; implies --realtime",
    )
    parser.add_argument(
        "--qgc-host",
        "--mavlink-host",
        dest="qgc_host",
        default="127.0.0.1",
        help="MAVLink UDP IPv4 address",
    )
    parser.add_argument(
        "--qgc-port",
        "--mavlink-port",
        dest="qgc_port",
        default="14550",
        help="MAVLink UDP port",
    )
    parser.add_argument("--mavlink-system-id", type=int, default=1, help="MAVLink system id")
    parser.add_argument(
        "--mavlink-source-port",
        type=int,
        default=0,
        help="optional local UDP source port for MAVLink packets",
    )


def validate_sitl_runner_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    if args.scenario != "cruise6dof" and args.initial is not None:
        parser.error("--initial is only supported with --scenario cruise6dof")
    if args.scenario != "cruise6dof" and args.case is not None:
        parser.error("--case is only supported with --scenario cruise6dof")
    if args.scenario != "cruise6dof" and args.conditions is not None:
        parser.error("--conditions is only supported with --scenario cruise6dof")
    if args.scenario != "cruise6dof" and args.profile != "cruise":
        parser.error("--profile is only supported with --scenario cruise6dof")
    if args.qgc and args.scenario != "cruise6dof":
        parser.error("--mavlink/--qgc is only supported with --scenario cruise6dof")
    if args.mavlink_system_id < 1 or args.mavlink_system_id > 255:
        parser.error("--mavlink-system-id must be in 1..255")
    if args.mavlink_source_port < 0 or args.mavlink_source_port > 65535:
        parser.error("--mavlink-source-port must be in 0..65535")


def sitl_runner_path(build_dir: Union[str, pathlib.Path]) -> pathlib.Path:
    return pathlib.Path(build_dir) / "vehicle" / "sitl_runner"


def build_sitl_runner_command(args: argparse.Namespace) -> list[str]:
    command = [
        str(sitl_runner_path(args.build_dir)),
        "--scenario",
        args.scenario,
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
    ]
    if args.initial is not None:
        command.extend(["--initial", args.initial])
    if args.case is not None:
        command.extend(["--case", args.case])
    if args.conditions is not None:
        command.extend(["--conditions", args.conditions])
    if args.realtime:
        command.append("--realtime")
    if args.qgc:
        command.extend(
            [
                "--mavlink",
                "--mavlink-host",
                args.qgc_host,
                "--mavlink-port",
                args.qgc_port,
                "--mavlink-system-id",
                str(args.mavlink_system_id),
            ]
        )
        if args.mavlink_source_port:
            command.extend(["--mavlink-source-port", str(args.mavlink_source_port)])
    return command

#!/usr/bin/env python3
"""Run sitl_runner and print summary metrics from the generated CSV."""

import argparse
import csv
import math
import pathlib
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="Run Altair SITL and summarize the CSV output.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--scenario", default="smoke", choices=("smoke", "cruise6dof"))
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=("cruise", "takeoff", "turn", "descent", "failsafe"),
        help="command profile for cruise6dof",
    )
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", default="sitl.csv")
    parser.add_argument("--initial", help="initial-condition file for cruise6dof")
    parser.add_argument("--frame-mode", choices=("ned", "ecef"), default="ecef", help="6DOF truth frame")
    parser.add_argument(
        "--realtime",
        action="store_true",
        help="pace the compiled runner so one simulated second takes one wall-clock second",
    )
    parser.add_argument(
        "--qgc",
        action="store_true",
        help="stream cruise6dof SITL telemetry to QGroundControl over MAVLink UDP; implies --realtime",
    )
    parser.add_argument("--qgc-host", default="127.0.0.1", help="QGroundControl UDP IPv4 address")
    parser.add_argument("--qgc-port", default="14550", help="QGroundControl UDP port")
    args = parser.parse_args()
    if args.scenario != "cruise6dof" and args.profile != "cruise":
        parser.error("--profile is only supported with --scenario cruise6dof")
    if args.qgc and args.scenario != "cruise6dof":
        parser.error("--qgc is only supported with --scenario cruise6dof")
    return args


def load_rows(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{csv_path}: no data rows")
    return rows


def numeric_column(rows, column):
    values = []
    for index, row in enumerate(rows, start=2):
        try:
            value = float(row[column])
        except KeyError as exc:
            raise ValueError(f"missing required column: {column}") from exc
        except ValueError as exc:
            raise ValueError(f"row {index}: column {column} is not numeric: {row[column]}") from exc
        values.append(value)
    return values


def all_numeric_values_are_finite(rows):
    for row in rows:
        for value in row.values():
            try:
                parsed = float(value)
            except ValueError:
                continue
            if not math.isfinite(parsed):
                return False
    return True


def max_abs(rows, column):
    return max(abs(value) for value in numeric_column(rows, column))


def print_metric(name, value):
    if isinstance(value, float):
        print(f"{name}={value:.6f}")
    else:
        print(f"{name}={value}")


def summarize(rows, scenario, profile, output_path):
    time_s = numeric_column(rows, "time_s")
    modes = numeric_column(rows, "mode")
    airspeeds = numeric_column(rows, "airspeed_mps")
    altitudes = numeric_column(rows, "altitude_m")

    print_metric("scenario", scenario)
    print_metric("profile", profile)
    print_metric("output", output_path)
    print_metric("rows", len(rows))
    print_metric("start_time_s", time_s[0])
    print_metric("end_time_s", time_s[-1])
    print_metric("final_mode", int(modes[-1]))
    print_metric("final_airspeed_mps", airspeeds[-1])
    print_metric("min_airspeed_mps", min(airspeeds))
    print_metric("max_airspeed_mps", max(airspeeds))
    print_metric("final_altitude_m", altitudes[-1])
    print_metric("min_altitude_m", min(altitudes))
    print_metric("max_altitude_m", max(altitudes))
    print_metric("finite", str(all_numeric_values_are_finite(rows)).lower())

    columns = rows[0].keys()
    if "roll_rad" in columns:
        print_metric("max_abs_roll_rad", max_abs(rows, "roll_rad"))
    if "pitch_rad" in columns:
        print_metric("max_abs_pitch_rad", max_abs(rows, "pitch_rad"))
    if "motor" in columns:
        motors = numeric_column(rows, "motor")
        print_metric("min_motor", min(motors))
        print_metric("max_motor", max(motors))


def main():
    args = parse_args()
    exe = pathlib.Path(args.build_dir) / "vehicle" / "sitl_runner"
    command = [
        str(exe),
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
    if args.realtime:
        command.append("--realtime")
    if args.qgc:
        command.extend(["--qgc", "--qgc-host", args.qgc_host, "--qgc-port", args.qgc_port])

    try:
        subprocess.run(command, check=True, text=True)
        rows = load_rows(args.output)
        summarize(rows, args.scenario, args.profile, args.output)
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"run_sitl.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

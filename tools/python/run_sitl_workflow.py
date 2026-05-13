#!/usr/bin/env python3
"""Run the routine Altair SITL workflow end to end."""

import argparse
import os
import pathlib
import shlex
import subprocess
import sys


def repo_root():
    return pathlib.Path(__file__).resolve().parents[2]


def parse_args():
    root = repo_root()
    parser = argparse.ArgumentParser(description="Run SITL, plot its CSV log, and generate 3D playback HTML.")
    parser.add_argument("--build-dir", default=str(root / "build"))
    parser.add_argument("--scenario", default="cruise6dof", choices=("smoke", "cruise6dof"))
    parser.add_argument(
        "--profile",
        default="cruise",
        choices=("cruise", "takeoff", "turn", "descent", "failsafe"),
        help="command profile for cruise6dof",
    )
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", default="sitl_cruise6dof.csv", help="CSV log path")
    parser.add_argument(
        "--initial",
        help="initial-condition file for cruise6dof; defaults to the repository cruise6dof fixture",
    )
    parser.add_argument(
        "--realtime",
        action="store_true",
        help="pace SITL in wall-clock time; default runs as fast as possible",
    )
    parser.add_argument(
        "--qgc",
        action="store_true",
        help="stream cruise6dof SITL telemetry to QGroundControl over MAVLink UDP; implies --realtime",
    )
    parser.add_argument("--qgc-host", default="127.0.0.1", help="QGroundControl UDP IPv4 address")
    parser.add_argument("--qgc-port", default="14550", help="QGroundControl UDP port")
    parser.add_argument(
        "--plot",
        action="append",
        choices=("velocities", "attitudes", "rates", "position", "all"),
        help="plot to generate; may be repeated",
    )
    parser.add_argument("--plots-dir", default="plots/sitl", help="directory for PNG plots")
    parser.add_argument("--html", default="sitl_3d.html", help="3D playback HTML output path")
    parser.add_argument("--title", default="Altair SITL 3D Playback")
    parser.add_argument("--skip-plots", action="store_true")
    parser.add_argument("--skip-visualization", action="store_true")
    args = parser.parse_args()
    if args.initial is None and args.scenario == "cruise6dof":
        args.initial = str(root / "tests" / "integration" / "cruise6dof_initial.ini")
    if args.scenario != "cruise6dof" and args.initial is not None:
        parser.error("--initial is only supported with --scenario cruise6dof")
    if args.scenario != "cruise6dof" and args.profile != "cruise":
        parser.error("--profile is only supported with --scenario cruise6dof")
    if args.qgc and args.scenario != "cruise6dof":
        parser.error("--qgc is only supported with --scenario cruise6dof")
    if args.scenario != "cruise6dof" and (not args.skip_plots or not args.skip_visualization):
        parser.error("non-cruise6dof scenarios require --skip-plots and --skip-visualization")
    return args


def run_step(label, command, env=None):
    print(f"\n== {label} ==", flush=True)
    print(shlex.join(str(part) for part in command), flush=True)
    subprocess.run(command, check=True, env=env, text=True)


def main():
    args = parse_args()
    root = repo_root()
    run_sitl = root / "tools" / "python" / "run_sitl.py"
    plot_sitl = root / "tools" / "python" / "plot_sitl.py"
    visualize = root / "tools" / "python" / "visualize_sitl_3d.py"

    sitl_command = [
        sys.executable,
        str(run_sitl),
        "--build-dir",
        args.build_dir,
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
    ]
    if args.initial:
        sitl_command.extend(["--initial", args.initial])
    if args.realtime:
        sitl_command.append("--realtime")
    if args.qgc:
        sitl_command.extend(["--qgc", "--qgc-host", args.qgc_host, "--qgc-port", args.qgc_port])

    try:
        run_step("Run SITL", sitl_command)

        if not args.skip_plots:
            plot_command = [sys.executable, str(plot_sitl), args.output, "--out-dir", args.plots_dir]
            for plot_name in args.plot or ["all"]:
                plot_command.extend(["--plot", plot_name])
            plot_env = os.environ.copy()
            if "MPLCONFIGDIR" not in plot_env:
                mpl_config_dir = pathlib.Path(args.plots_dir) / ".matplotlib"
                mpl_config_dir.mkdir(parents=True, exist_ok=True)
                plot_env["MPLCONFIGDIR"] = str(mpl_config_dir)
            run_step("Generate plots", plot_command, env=plot_env)

        if not args.skip_visualization:
            run_step(
                "Generate 3D visualization",
                [
                    sys.executable,
                    str(visualize),
                    args.output,
                    "--output",
                    args.html,
                    "--title",
                    args.title,
                ],
            )
    except subprocess.CalledProcessError as exc:
        print(f"run_sitl_workflow.py: {exc}", file=sys.stderr)
        return exc.returncode
    except OSError as exc:
        print(f"run_sitl_workflow.py: {exc}", file=sys.stderr)
        return 1

    print("\n== Outputs ==")
    print(f"csv={args.output}")
    if not args.skip_plots:
        print(f"plots_dir={args.plots_dir}")
    if not args.skip_visualization:
        print(f"html={args.html}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Run the compiled Monte Carlo executable and write CSV output."""

import argparse
import pathlib
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--scenario", default="smoke")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--dt", type=float, default=0.01)
    parser.add_argument("--output", default="mc_summary.csv")
    args = parser.parse_args()

    exe = pathlib.Path(args.build_dir) / "vehicle" / "mc_runner"
    subprocess.run(
        [
            str(exe),
            "--seed",
            str(args.seed),
            "--runs",
            str(args.runs),
            "--scenario",
            args.scenario,
            "--duration",
            str(args.duration),
            "--dt",
            str(args.dt),
            "--output",
            args.output,
        ],
        check=True,
        text=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

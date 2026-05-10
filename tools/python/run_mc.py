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
    parser.add_argument("--output", default="mc_summary.csv")
    args = parser.parse_args()

    exe = pathlib.Path(args.build_dir) / "vehicle" / "mc_runner"
    result = subprocess.run(
        [str(exe), str(args.seed), str(args.runs)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    pathlib.Path(args.output).write_text(result.stdout, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

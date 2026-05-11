#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def parse_metrics(text):
    metrics = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            metrics[key] = value
    return metrics


def main():
    if len(sys.argv) != 4:
        print("usage: test_run_sitl.py <repo-root> <build-dir> <tmp-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    build_dir = Path(sys.argv[2])
    tmp_dir = Path(sys.argv[3])
    tmp_dir.mkdir(parents=True, exist_ok=True)
    csv_path = tmp_dir / "sitl_cruise6dof.csv"

    result = subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/run_sitl.py"),
            "--build-dir",
            str(build_dir),
            "--scenario",
            "cruise6dof",
            "--initial",
            str(repo_root / "tests/integration/cruise6dof_initial.ini"),
            "--duration",
            "0.2",
            "--dt",
            "0.01",
            "--output",
            str(csv_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode

    metrics = parse_metrics(result.stdout)
    required_keys = (
        "scenario",
        "output",
        "rows",
        "end_time_s",
        "final_mode",
        "final_airspeed_mps",
        "final_altitude_m",
        "finite",
        "max_abs_roll_rad",
        "max_motor",
    )
    missing = [key for key in required_keys if key not in metrics]
    if missing:
        print(f"missing metric(s): {', '.join(missing)}", file=sys.stderr)
        return 1
    if metrics["scenario"] != "cruise6dof":
        print("scenario metric was not cruise6dof", file=sys.stderr)
        return 1
    if metrics["finite"] != "true":
        print("finite metric was not true", file=sys.stderr)
        return 1
    if int(metrics["rows"]) != 20:
        print(f"expected 20 rows, got {metrics['rows']}", file=sys.stderr)
        return 1
    if not csv_path.exists():
        print(f"{csv_path} was not created", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

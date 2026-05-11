#!/usr/bin/env python3

import os
import subprocess
import sys
from pathlib import Path


def main():
    if "MPLCONFIGDIR" not in os.environ:
        mpl_config_dir = Path("/tmp") / "altair_test_matplotlib"
        mpl_config_dir.mkdir(parents=True, exist_ok=True)
        os.environ["MPLCONFIGDIR"] = str(mpl_config_dir)
    try:
        import matplotlib  # noqa: F401
    except ImportError:
        return 77

    if len(sys.argv) != 4:
        print("usage: test_sitl_workflow.py <repo-root> <build-dir> <tmp-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    build_dir = Path(sys.argv[2])
    tmp_dir = Path(sys.argv[3])
    tmp_dir.mkdir(parents=True, exist_ok=True)
    csv_path = tmp_dir / "sitl_cruise6dof.csv"
    plots_dir = tmp_dir / "plots"
    html_path = tmp_dir / "sitl_3d.html"

    result = subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/run_sitl_workflow.py"),
            "--build-dir",
            str(build_dir),
            "--duration",
            "0.2",
            "--dt",
            "0.01",
            "--output",
            str(csv_path),
            "--plots-dir",
            str(plots_dir),
            "--html",
            str(html_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode

    expected_outputs = (
        csv_path,
        plots_dir / "velocities.png",
        plots_dir / "attitudes.png",
        plots_dir / "rates.png",
        plots_dir / "position_latlon.png",
        plots_dir / "position_3d.png",
        html_path,
    )
    missing = [str(path) for path in expected_outputs if not path.exists()]
    if missing:
        print(f"missing output(s): {', '.join(missing)}", file=sys.stderr)
        return 1

    if "scenario=cruise6dof" not in result.stdout:
        print("workflow did not use cruise6dof by default", file=sys.stderr)
        return 1
    if "--realtime" in result.stdout:
        print("workflow unexpectedly enabled realtime by default", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

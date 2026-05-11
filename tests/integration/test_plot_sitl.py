#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def main():
    try:
        import matplotlib  # noqa: F401
    except ImportError:
        return 77

    if len(sys.argv) != 3:
        print("usage: test_plot_sitl.py <repo-root> <tmp-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    tmp_dir = Path(sys.argv[2])
    tmp_dir.mkdir(parents=True, exist_ok=True)
    csv_path = tmp_dir / "sitl_fixture.csv"
    out_dir = tmp_dir / "plots"
    csv_path.write_text(
        "step,time_s,vel_n_mps,vel_e_mps,vel_d_mps,roll_rad,pitch_rad,yaw_rad,p_rps,q_rps,r_rps,lat_deg,lon_deg,pos_n_m,pos_e_m,altitude_m\n"
        "0,0.0,10,0,0,0,0,0,0,0,0,37.0,-122.0,0,0,100\n"
        "1,0.1,10.5,0.2,-0.1,0.01,0.02,0.03,0.1,0.2,0.3,37.00001,-121.99999,1,0.2,100.1\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/plot_sitl.py"),
            str(csv_path),
            "--plot",
            "velocities",
            "--out-dir",
            str(out_dir),
        ],
        check=False,
    )
    if result.returncode != 0:
        return result.returncode
    if not (out_dir / "velocities.png").exists():
        print("velocities.png was not created", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

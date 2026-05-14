#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        print("usage: test_visualize_sitl_3d.py <repo-root> <tmp-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    tmp_dir = Path(sys.argv[2])
    tmp_dir.mkdir(parents=True, exist_ok=True)
    csv_path = tmp_dir / "sitl_fixture.csv"
    html_path = tmp_dir / "sitl_3d.html"
    csv_path.write_text(
        "step,time_s,pos_n_m,pos_e_m,pos_d_m,altitude_m,roll_rad,pitch_rad,yaw_rad,"
        "airspeed_mps,mode,lat_deg,lon_deg,vel_n_mps,vel_e_mps,vel_d_mps,"
        "p_rps,q_rps,r_rps,accel_x_mps2,accel_y_mps2,accel_z_mps2,"
        "force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm\n"
        "0,0.0,0,0,-100,100,0,0,0,18,1,37.4275,-122.1697,18,0,-0.1,0.01,0.02,0.03,1,2,-9.8,3,4,5,0.1,0.2,0.3\n"
        "1,0.1,2,0.4,-100.2,100.2,0.01,0.02,0.03,18.1,1,37.4276,-122.1698,18.1,0.3,-0.2,0.02,0.03,0.04,1.1,2.1,-9.7,3.1,4.1,5.1,0.2,0.3,0.4\n"
        "2,0.2,4,0.9,-100.5,100.5,0.02,0.03,0.04,18.2,1,37.4277,-122.1699,18.2,0.5,-0.3,0.03,0.04,0.05,1.2,2.2,-9.6,3.2,4.2,5.2,0.3,0.4,0.5\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/visualize_sitl_3d.py"),
            str(csv_path),
            "--output",
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
    if not html_path.exists():
        print("sitl_3d.html was not created", file=sys.stderr)
        return 1
    text = html_path.read_text(encoding="utf-8")
    required_fragments = (
        '<canvas id="view">',
        "metrics-panel",
        "Lock on aircraft",
        'id="zoom" type="range" min="0" max="8" value="2"',
        "application/json",
        '"frames":[',
        '"pos_n_m":4.0',
        '"lat_deg":37.4277',
        '"vel_n_mps":18.2',
        '"p_rps":0.03',
        '"accel_x_mps2":1.2',
        '"force_x_n":3.2',
        '"moment_z_nm":0.5',
        'TRAIL_COLOR = "#8b5cf6"',
        "zoomMultipliers = [0.5, 0.75, 1, 1.5, 2, 3, 5, 7.5, 10]",
        "function drawAircraftMesh",
        "function rotateBody",
        "function sceneCenter",
        "requestAnimationFrame(tick)",
    )
    missing = [fragment for fragment in required_fragments if fragment not in text]
    if missing:
        print(f"missing HTML fragment(s): {', '.join(missing)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

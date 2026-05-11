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
        "step,time_s,pos_n_m,pos_e_m,altitude_m,roll_rad,pitch_rad,yaw_rad,airspeed_mps,mode\n"
        "0,0.0,0,0,100,0,0,0,18,1\n"
        "1,0.1,2,0.4,100.2,0.01,0.02,0.03,18.1,1\n"
        "2,0.2,4,0.9,100.5,0.02,0.03,0.04,18.2,1\n",
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
        "<canvas id=\"view\">",
        "application/json",
        "\"frames\":[",
        "\"pos_n_m\":4.0",
        "requestAnimationFrame(tick)",
    )
    missing = [fragment for fragment in required_fragments if fragment not in text]
    if missing:
        print(f"missing HTML fragment(s): {', '.join(missing)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

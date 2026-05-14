#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def run_session(repo_root, *args):
    return subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/run_sitl_session.py"),
            "--dry-run",
            *args,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def main():
    if len(sys.argv) != 2:
        print("usage: test_sitl_session.py <repo-root>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    result = run_session(repo_root, "--duration", "0.2", "--output", "sitl_live_test.csv")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if "mavlink_live_bridge.py" not in result.stdout:
        print("session did not start the MAVLink bridge", file=sys.stderr)
        return 1
    if "--forward 127.0.0.1:14550" not in result.stdout:
        print("session did not forward to QGC by default", file=sys.stderr)
        return 1
    if "npm run dev -- --host 127.0.0.1 --port 5173" not in result.stdout:
        print("session did not start the live viewer by default", file=sys.stderr)
        return 1
    if "--mavlink-port 14551" not in result.stdout or "--realtime" not in result.stdout:
        print("session did not route realtime SITL through the bridge", file=sys.stderr)
        return 1
    if "tests/integration/cruise6dof_initial.ini" not in result.stdout:
        print("session did not use the repository initial-condition fixture by default", file=sys.stderr)
        return 1

    result = run_session(repo_root, "--no-viewer", "--no-qgc")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if "viewer:" in result.stdout:
        print("session started the viewer despite --no-viewer", file=sys.stderr)
        return 1
    if "--no-forward" not in result.stdout:
        print("session did not disable bridge forwarding with --no-qgc", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

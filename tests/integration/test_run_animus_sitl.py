#!/usr/bin/env python3

import subprocess
import sys
from pathlib import Path


def run_launcher(repo_root, *args):
    return subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/run_animus_sitl.py"),
            "--dry-run",
            *args,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def main():
    if len(sys.argv) != 2:
        print("usage: test_run_animus_sitl.py <repo-root>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    result = run_launcher(repo_root, "--duration", "0.2", "--output", "sitl_animus_test.csv")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode

    checks = (
        ("cmake -S", "launcher did not configure builds by default"),
        ("--target sitl_runner", "launcher did not build sitl_runner"),
        ("--target animus_qt", "launcher did not build animus_qt"),
        (
            "--start-udp-telemetry --udp-host 127.0.0.1 --udp-port 14551",
            "Animus UDP startup missing",
        ),
        ("tools/python/run_sitl.py", "launcher did not run the SITL helper"),
        ("--scenario cruise6dof", "launcher did not select cruise6dof"),
        ("--realtime --mavlink", "launcher did not request realtime MAVLink output"),
        ("--mavlink-port 14551", "launcher did not route SITL to Animus UDP"),
        ("--mavlink-source-port 14600", "launcher did not set predictable source port"),
        (
            "tests/integration/cruise6dof_initial.ini",
            "launcher did not use the default initial fixture",
        ),
    )
    for needle, message in checks:
        if needle not in result.stdout:
            print(message, file=sys.stderr)
            print(result.stdout, end="")
            return 1

    result = run_launcher(repo_root, "--skip-build", "--udp-port", "27551", "--case", "case.ini")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if "build 1:" in result.stdout:
        print("--skip-build still printed build commands", file=sys.stderr)
        return 1
    if "--udp-port 27551" not in result.stdout or "--mavlink-port 27551" not in result.stdout:
        print("custom UDP port was not applied to both Animus and SITL", file=sys.stderr)
        return 1
    if "--case case.ini" not in result.stdout or "--initial" in result.stdout:
        print("--case did not replace the default --initial path", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

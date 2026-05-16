#!/usr/bin/env python3
"""Run selected Altair verification checks and collect logs/artifacts."""

import argparse
import datetime
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run selected Altair verification checks with an artifact manifest."
    )
    parser.add_argument("--format", action="store_true", help="run format check")
    parser.add_argument(
        "--cmake", action="store_true", help="configure, build, and test Debug tree"
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="configure, build, and test Release tree with warnings as errors",
    )
    parser.add_argument("--sitl-plots", action="store_true", help="run cruise6dof SITL and plots")
    parser.add_argument("--mc", action="store_true", help="run Monte Carlo smoke summary")
    parser.add_argument(
        "--animus", action="store_true", help="run Animus test/build/capture workflow"
    )
    parser.add_argument("--all", action="store_true", help="run every verification check")
    return parser.parse_args()


def timestamp():
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.strftime("%Y%m%dT%H%M%SZ")


def command_to_string(command):
    return " ".join(str(part) for part in command)


def run_command(name, command, artifact_dir):
    log_path = artifact_dir / f"{name}.log"
    started = time.time()
    result = {
        "name": name,
        "command": [str(part) for part in command],
        "log": str(log_path),
        "status": "running",
        "returncode": None,
        "duration_s": None,
    }
    with log_path.open("w", encoding="utf-8") as log:
        log.write(f"$ {command_to_string(command)}\n\n")
        log.flush()
        completed = subprocess.run(
            [str(part) for part in command],
            cwd=ROOT,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    result["returncode"] = completed.returncode
    result["duration_s"] = round(time.time() - started, 3)
    result["status"] = "pass" if completed.returncode == 0 else "fail"
    return result


def add_check(checks, name, commands, artifacts=None):
    checks.append({"name": name, "commands": commands, "artifacts": artifacts or []})


def selected_checks(args, artifact_dir):
    checks = []
    if args.all or args.format:
        add_check(
            checks,
            "format",
            [[sys.executable, "tools/python/format_repo.py", "--check"]],
        )
    if args.all or args.cmake:
        add_check(
            checks,
            "cmake",
            [
                ["cmake", "-S", ".", "-B", "build"],
                ["cmake", "--build", "build", "--parallel"],
                ["ctest", "--test-dir", "build", "--output-on-failure"],
            ],
        )
    if args.all or args.release:
        add_check(
            checks,
            "release",
            [
                [
                    "cmake",
                    "-S",
                    ".",
                    "-B",
                    "build-release",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DALTAIR_WARNINGS_AS_ERRORS=ON",
                ],
                ["cmake", "--build", "build-release", "--parallel"],
                ["ctest", "--test-dir", "build-release", "--output-on-failure"],
            ],
        )
    if args.all or args.sitl_plots:
        csv_path = artifact_dir / "sitl_cruise6dof.csv"
        plots_dir = artifact_dir / "plots" / "sitl"
        add_check(
            checks,
            "sitl_plots",
            [
                [
                    sys.executable,
                    "tools/python/run_sitl.py",
                    "--build-dir",
                    "build",
                    "--scenario",
                    "cruise6dof",
                    "--initial",
                    "tests/integration/cruise6dof_initial.ini",
                    "--duration",
                    "60",
                    "--dt",
                    "0.01",
                    "--output",
                    csv_path,
                    "--plot",
                    "all",
                    "--plots-dir",
                    plots_dir,
                ]
            ],
            [csv_path, plots_dir],
        )
    if args.all or args.mc:
        mc_csv = artifact_dir / "mc_summary.csv"
        add_check(
            checks,
            "mc",
            [
                [
                    "build/vehicle/mc_runner",
                    "--seed",
                    "1",
                    "--runs",
                    "100",
                    "--scenario",
                    "smoke",
                    "--output",
                    mc_csv,
                ]
            ],
            [mc_csv],
        )
    if args.all or args.animus:
        add_check(
            checks,
            "animus",
            [
                ["npm", "test", "--prefix", "tools/animus"],
                ["npm", "run", "build", "--prefix", "tools/animus"],
                [sys.executable, "tools/python/capture_animus_sitl.py"],
            ],
        )
    return checks


def run_check(check, artifact_dir):
    step_results = []
    status = "pass"
    for index, command in enumerate(check["commands"], start=1):
        result = run_command(f"{check['name']}_{index}", command, artifact_dir)
        step_results.append(result)
        if result["status"] != "pass":
            status = "fail"
            break
    return {
        "name": check["name"],
        "status": status,
        "steps": step_results,
        "artifacts": [str(path) for path in check["artifacts"]],
    }


def write_manifest(manifest_path, manifest):
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main():
    args = parse_args()
    artifact_dir = ROOT / "artifacts" / "agent-verification" / timestamp()
    checks = selected_checks(args, artifact_dir)
    if not checks:
        print("verify_agent_work.py: select at least one check or use --all", file=sys.stderr)
        return 2

    artifact_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = artifact_dir / "manifest.json"
    manifest = {
        "artifact_dir": str(artifact_dir),
        "checks": [],
        "status": "running",
    }
    write_manifest(manifest_path, manifest)

    print(f"artifactDir={artifact_dir}")
    overall_status = "pass"
    for check in checks:
        result = run_check(check, artifact_dir)
        manifest["checks"].append(result)
        write_manifest(manifest_path, manifest)
        print(f"{result['name']}={result['status']}")
        if result["status"] != "pass":
            overall_status = "fail"

    manifest["status"] = overall_status
    manifest["manifest"] = str(manifest_path)
    write_manifest(manifest_path, manifest)
    print(f"manifest={manifest_path}")
    print(f"status={overall_status}")
    return 0 if overall_status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())

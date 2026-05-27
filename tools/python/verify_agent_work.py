#!/usr/bin/env python3
"""Recommend or run selected Altair verification checks."""

import argparse
import datetime
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
RECOMMENDATION_ARTIFACT_DIR = pathlib.Path("artifacts/agent-verification/<timestamp>")
CHECK_ORDER = ("format", "cmake", "release", "sitl_plots", "sitl_live", "mc")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Recommend checks for changed paths or run selected checks with artifacts."
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
    parser.add_argument(
        "--sitl-live",
        action="store_true",
        help="run cruise6dof SITL MAVLink into Animus direct live UDP capture",
    )
    parser.add_argument("--mc", action="store_true", help="run Monte Carlo smoke summary")
    parser.add_argument("--all", action="store_true", help="run every verification check")
    parser.add_argument(
        "--recommend",
        action="store_true",
        help="print path-aware recommended checks for changed files without running them",
    )
    return parser.parse_args()


def timestamp():
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.strftime("%Y%m%dT%H%M%SZ")


def command_to_string(command):
    return " ".join(str(part) for part in command)


def normalize_repo_path(path):
    return pathlib.PurePosixPath(str(path).replace("\\", "/").lstrip("./")).as_posix()


def is_doc_path(path):
    return path.endswith(".md") or path.startswith("docs/") or path.startswith("bayek/docs/")


def is_c_or_build_path(path):
    suffix = pathlib.PurePosixPath(path).suffix
    return suffix in {".c", ".h", ".cmake"} or pathlib.PurePosixPath(path).name == "CMakeLists.txt"


def is_core_path(path):
    return (
        is_c_or_build_path(path)
        or path.startswith(
            (
                "params/",
                "config/",
                "mixer/",
                "vehicle/",
                "boards/",
                "bayek/common/",
                "bayek/fsw/",
                "bayek/host/",
                "bayek/sim/",
                "bayek/telemetry/",
            )
        )
        or path in {"CMakeLists.txt", "tests/CMakeLists.txt"}
    )


def is_sitl_path(path):
    name = pathlib.PurePosixPath(path).name
    return (
        "sitl" in path
        or "simulation" in path
        or "telemetry" in path
        or "replay" in path
        or "mavlink" in path
        or "sitl_runner" in name
        or "run_sitl" in name
        or path.startswith("tests/integration/cruise6dof_")
        or path.startswith("tests/integration/fixtures/")
        or path.startswith("bayek/sim/")
        or path.startswith("bayek/host/sitl_")
    )


def is_mc_path(path):
    return (
        "mc_runner" in path
        or "monte" in path.lower()
        or "guardrail" in path.lower()
        or "check_mc_summary" in path
    )


def add_reason(reasons, check_name, reason):
    reasons.setdefault(check_name, [])
    if reason not in reasons[check_name]:
        reasons[check_name].append(reason)


def select_verification_for_paths(paths):
    changed_paths = sorted({normalize_repo_path(path) for path in paths if str(path).strip()})
    reasons = {}
    if not changed_paths:
        return []

    if all(is_doc_path(path) for path in changed_paths):
        add_reason(
            reasons,
            "format",
            "documentation-only changes; run the lightweight formatting gate unless the docs "
            "also change generated contracts",
        )
    else:
        for path in changed_paths:
            if is_doc_path(path):
                add_reason(reasons, "format", "documentation changed alongside code")
                continue
            if pathlib.PurePosixPath(path).suffix in {".c", ".h", ".py", ".cmake"} or (
                pathlib.PurePosixPath(path).name == "CMakeLists.txt"
            ):
                add_reason(reasons, "format", "formatted C, Python, or CMake source changed")
            if is_core_path(path):
                add_reason(
                    reasons,
                    "cmake",
                    "C, CMake, core parameter, vehicle, or Bayek integration path changed",
                )
                add_reason(
                    reasons,
                    "release",
                    "shared C/build path changed; include Release warnings-as-errors coverage",
                )
            if is_sitl_path(path):
                add_reason(
                    reasons,
                    "cmake",
                    "SITL, simulation, telemetry, runner, or case path changed",
                )
                add_reason(
                    reasons,
                    "sitl_plots",
                    "SITL/simulation behavior may affect generated CSVs or plots",
                )
                add_reason(
                    reasons,
                    "sitl_live",
                    "MAVLink or telemetry changes may affect live Animus UDP ingestion",
                )
            if is_mc_path(path):
                add_reason(reasons, "cmake", "Monte Carlo runner or guardrail path changed")
                add_reason(reasons, "mc", "Monte Carlo policy or summary semantics may change")

        if not reasons:
            add_reason(
                reasons,
                "format",
                "changed paths do not match a heavier verification group",
            )

    checks_by_name = {check["name"]: check for check in selected_checks_for_flags(CHECK_ORDER)}
    recommendations = []
    for check_name in CHECK_ORDER:
        if check_name in reasons:
            check = dict(checks_by_name[check_name])
            check["rationale"] = "; ".join(reasons[check_name])
            recommendations.append(check)
    return recommendations


def changed_files_from_git():
    commands = (
        ["git", "diff", "--name-only"],
        ["git", "diff", "--name-only", "--cached"],
        ["git", "ls-files", "--others", "--exclude-standard"],
    )
    changed = set()
    for command in commands:
        result = subprocess.run(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr, end="")
            raise SystemExit(result.returncode)
        changed.update(line.strip() for line in result.stdout.splitlines() if line.strip())
    return sorted(changed)


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


def selected_checks_for_flags(flags, artifact_dir=RECOMMENDATION_ARTIFACT_DIR):
    requested = set(flags)
    checks = []
    if "all" in requested or "format" in requested:
        add_check(
            checks,
            "format",
            [[sys.executable, "tools/python/format_repo.py", "--check"]],
        )
    if "all" in requested or "cmake" in requested:
        add_check(
            checks,
            "cmake",
            [
                ["cmake", "-S", ".", "-B", "build"],
                ["cmake", "--build", "build", "--parallel"],
                ["ctest", "--test-dir", "build", "--output-on-failure"],
            ],
        )
    if "all" in requested or "release" in requested:
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
    if "all" in requested or "sitl_plots" in requested:
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
    if "all" in requested or "sitl_live" in requested:
        live_dir = artifact_dir / "animus_sitl_live"
        add_check(
            checks,
            "sitl_live",
            [
                [
                    sys.executable,
                    "tools/python/run_animus_sitl_live_smoke.py",
                    "--animus-build-dir",
                    "animus/build",
                    "--vehicle-build-dir",
                    "build",
                    "--output-dir",
                    live_dir,
                ]
            ],
            [live_dir],
        )
    if "all" in requested or "mc" in requested:
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
    return checks


def selected_checks(args, artifact_dir):
    flags = []
    for flag in CHECK_ORDER:
        attr = flag
        if flag == "sitl_plots":
            attr = "sitl_plots"
        elif flag == "sitl_live":
            attr = "sitl_live"
        if getattr(args, attr, False):
            flags.append(flag)
    if args.all:
        flags.append("all")
    return selected_checks_for_flags(flags, artifact_dir)


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


def has_execution_flag(args):
    return (
        args.all
        or args.format
        or args.cmake
        or args.release
        or args.sitl_plots
        or args.sitl_live
        or args.mc
    )


def print_recommendations(changed_files, recommendations):
    print("verify_agent_work.py: recommended checks for changed files")
    if changed_files:
        print("changedFiles=" + ",".join(changed_files))
    else:
        print("changedFiles=<none>")
        print("No changed files detected by git diff, staged diff, or untracked file scan.")
        return

    if not recommendations:
        print("recommendation=<none>")
        return

    for check in recommendations:
        print(f"\n[{check['name']}]")
        print(f"rationale={check['rationale']}")
        for command in check["commands"]:
            print(command_to_string(command))


def main():
    args = parse_args()
    if args.recommend or not has_execution_flag(args):
        changed_files = changed_files_from_git()
        recommendations = select_verification_for_paths(changed_files)
        print_recommendations(changed_files, recommendations)
        return 0

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

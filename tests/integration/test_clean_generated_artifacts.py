#!/usr/bin/env python3

import importlib.util
import subprocess
import sys
from pathlib import Path


def load_clean_generated_artifacts(repo_root):
    path = repo_root / "tools/python/clean_generated_artifacts.py"
    spec = importlib.util.spec_from_file_location("clean_generated_artifacts", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise RuntimeError(f"command failed: {' '.join(command)}")


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def create_fixture_repo(tmp_dir):
    repo = tmp_dir / "cleanup-fixture"
    repo.mkdir(parents=True, exist_ok=True)
    run(["git", "init"], repo)
    write_text(
        repo / ".gitignore",
        "\n".join(
            (
                "build/",
                "build-*/",
                "artifacts/",
                "map_cache/",
                "plots/",
                "/*.csv",
                "/sitl_3d.html",
                "__pycache__/",
                "",
            )
        ),
    )
    write_text(repo / "build/output.o", "generated")
    write_text(repo / "build-release/output.o", "generated")
    write_text(repo / "artifacts/run/log.txt", "generated")
    write_text(repo / "plots/sitl/plot.png", "generated")
    write_text(repo / "map_cache/tile.bin", "generated")
    write_text(repo / "sitl_live.csv", "generated")
    write_text(repo / "sitl_3d.html", "generated")
    write_text(repo / "tools/python/__pycache__/helper.pyc", "generated")
    write_text(repo / "notes.csv", "not ignored outside root rule only if root")
    write_text(repo / "logs/debug.log", "not ignored")
    write_text(repo / "tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv", "tracked")
    run(
        ["git", "add", ".gitignore", "tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv"],
        repo,
    )
    return repo


def expect(condition, message):
    if not condition:
        print(message, file=sys.stderr)
        return 1
    return 0


def main():
    if len(sys.argv) != 3:
        print("usage: test_clean_generated_artifacts.py <repo-root> <tmp-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    tmp_dir = Path(sys.argv[2])
    module = load_clean_generated_artifacts(repo_root)
    fixture = create_fixture_repo(tmp_dir)

    expected = {
        Path("artifacts"),
        Path("build"),
        Path("build-release"),
        Path("map_cache"),
        Path("notes.csv"),
        Path("plots"),
        Path("sitl_3d.html"),
        Path("sitl_live.csv"),
        Path("tools/python/__pycache__"),
    }
    actual = set(module.removable_artifacts(fixture))
    status = 0
    status |= expect(actual == expected, f"unexpected removable artifacts: {sorted(actual)}")

    status |= expect(
        (fixture / "build/output.o").exists(), "dry run selection deleted build output"
    )
    status |= expect((fixture / "sitl_live.csv").exists(), "dry run selection deleted root CSV")
    status |= expect(
        (fixture / "tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv").exists(),
        "tracked replay fixture was removed",
    )
    status |= expect((fixture / "logs/debug.log").exists(), "non-ignored file was removed")

    for relative_path in actual:
        module.remove_path(fixture / relative_path)

    for relative_path in expected:
        status |= expect(
            not (fixture / relative_path).exists(),
            f"ignored generated path still exists after apply: {relative_path}",
        )
    status |= expect(
        (fixture / "tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv").exists(),
        "tracked replay fixture did not survive apply",
    )
    status |= expect(
        (fixture / "logs/debug.log").exists(), "non-ignored file did not survive apply"
    )
    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())

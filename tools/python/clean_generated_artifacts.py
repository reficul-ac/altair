#!/usr/bin/env python3
"""Preview or remove ignored generated artifacts from an Altair checkout."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run_git(repo_root: Path, args: list[str], path: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", *args, "--", path.as_posix()],
        cwd=repo_root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def is_tracked(repo_root: Path, path: Path) -> bool:
    result = run_git(repo_root, ["ls-files", "--error-unmatch"], path)
    return result.returncode == 0


def is_ignored(repo_root: Path, path: Path) -> bool:
    result = run_git(repo_root, ["check-ignore", "-q"], path)
    return result.returncode == 0


def iter_existing(paths: list[Path]) -> list[Path]:
    return [path for path in paths if path.exists() or path.is_symlink()]


def find_pycache_dirs(repo_root: Path) -> list[Path]:
    pycache_dirs: list[Path] = []
    for path in repo_root.rglob("__pycache__"):
        relative_parts = path.relative_to(repo_root).parts
        if ".git" in relative_parts:
            continue
        if path.is_dir() or path.is_symlink():
            pycache_dirs.append(path.relative_to(repo_root))
    return pycache_dirs


def generated_artifact_candidates(repo_root: Path) -> list[Path]:
    candidates: list[Path] = []
    candidates.extend(iter_existing([repo_root / "build"]))
    candidates.extend(path for path in repo_root.glob("build-*") if path.exists())
    candidates.extend(
        iter_existing(
            [
                repo_root / "artifacts",
                repo_root / "plots",
                repo_root / "map_cache",
                repo_root / "sitl_3d.html",
            ]
        )
    )
    candidates.extend(path for path in repo_root.glob("*.csv") if path.exists())
    candidates.extend(repo_root / path for path in find_pycache_dirs(repo_root))

    relative_candidates = {
        path.relative_to(repo_root)
        for path in candidates
        if path == repo_root or repo_root in path.parents
    }
    return sorted(relative_candidates, key=lambda path: path.as_posix())


def removable_artifacts(repo_root: Path) -> list[Path]:
    removable: list[Path] = []
    for path in generated_artifact_candidates(repo_root):
        if is_tracked(repo_root, path):
            continue
        if is_ignored(repo_root, path):
            removable.append(path)
    return removable


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="delete ignored generated artifacts instead of printing a dry run",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = ROOT
    artifacts = removable_artifacts(repo_root)

    if not artifacts:
        print("No ignored generated artifacts found.")
        return 0

    action = "Removing" if args.apply else "Would remove"
    for path in artifacts:
        print(f"{action}: {path.as_posix()}")
        if args.apply:
            remove_path(repo_root / path)

    if not args.apply:
        print("\nDry run only. Re-run with --apply to delete these paths.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

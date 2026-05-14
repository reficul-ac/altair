#!/usr/bin/env python3
"""Format Altair-owned source files."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


CLANG_EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hpp"}
PYTHON_EXTENSIONS = {".py"}
CMAKE_NAMES = {"CMakeLists.txt"}
CMAKE_EXTENSIONS = {".cmake"}

EXCLUDED_PREFIXES = (
    "bayek/",
    "build/",
    "tools/live_viewer/node_modules/",
    "tools/live_viewer/dist/",
)
EXCLUDED_PARTS = {
    "__pycache__",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
}
EXCLUDED_SUFFIXES = (
    ".log",
    ".csv",
    ".html",
    ".json",
    ".wasm",
    ".map",
)


def run(args: list[str], *, cwd: Path, capture: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(
        args,
        cwd=cwd,
        check=False,
        text=False,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )


def require_tools(tools: list[str]) -> int:
    missing = [tool for tool in tools if shutil.which(tool) is None]
    if missing:
        print(
            "missing formatter tool(s): "
            + ", ".join(missing)
            + "\nInstall clang-format, black, and cmake-format.",
            file=sys.stderr,
        )
        return 1
    return 0


def git_files(repo_root: Path) -> list[Path]:
    result = run(["git", "ls-files"], cwd=repo_root, capture=True)
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace") if result.stderr else ""
        print(stderr, file=sys.stderr, end="")
        raise SystemExit(result.returncode)

    files: list[Path] = []
    for raw_path in result.stdout.decode("utf-8").splitlines():
        if is_excluded(raw_path):
            continue
        path = Path(raw_path)
        if is_formattable(path):
            files.append(path)
    return files


def is_excluded(path: str) -> bool:
    if path.startswith(EXCLUDED_PREFIXES):
        return True
    if path.endswith(EXCLUDED_SUFFIXES):
        return True
    return any(part in EXCLUDED_PARTS for part in Path(path).parts)


def is_formattable(path: Path) -> bool:
    return (
        path.suffix in CLANG_EXTENSIONS
        or path.suffix in PYTHON_EXTENSIONS
        or path.suffix in CMAKE_EXTENSIONS
        or path.name in CMAKE_NAMES
    )


def split_files(files: list[Path]) -> tuple[list[Path], list[Path], list[Path]]:
    clang_files: list[Path] = []
    python_files: list[Path] = []
    cmake_files: list[Path] = []

    for path in files:
        if path.suffix in CLANG_EXTENSIONS:
            clang_files.append(path)
        elif path.suffix in PYTHON_EXTENSIONS:
            python_files.append(path)
        elif path.suffix in CMAKE_EXTENSIONS or path.name in CMAKE_NAMES:
            cmake_files.append(path)

    return clang_files, python_files, cmake_files


def check_clang(repo_root: Path, files: list[Path]) -> int:
    status = 0
    for path in files:
        result = run(["clang-format", str(path)], cwd=repo_root, capture=True)
        if result.returncode != 0:
            stderr = result.stderr.decode("utf-8", errors="replace") if result.stderr else ""
            print(stderr, file=sys.stderr, end="")
            status = result.returncode
            continue
        if result.stdout != (repo_root / path).read_bytes():
            print(f"would reformat {path}")
            status = 1
    return status


def check_cmake(repo_root: Path, files: list[Path]) -> int:
    status = 0
    for path in files:
        result = run(["cmake-format", str(path)], cwd=repo_root, capture=True)
        if result.returncode != 0:
            stderr = result.stderr.decode("utf-8", errors="replace") if result.stderr else ""
            print(stderr, file=sys.stderr, end="")
            status = result.returncode
            continue
        if result.stdout != (repo_root / path).read_bytes():
            print(f"would reformat {path}")
            status = 1
    return status


def check_black(repo_root: Path, files: list[Path]) -> int:
    if not files:
        return 0
    return run(["black", "--check", "--quiet", *map(str, files)], cwd=repo_root).returncode


def fix_files(repo_root: Path, clang_files: list[Path], python_files: list[Path], cmake_files: list[Path]) -> int:
    commands: list[list[str]] = []
    if clang_files:
        commands.append(["clang-format", "-i", *map(str, clang_files)])
    if python_files:
        commands.append(["black", "--quiet", *map(str, python_files)])
    if cmake_files:
        commands.append(["cmake-format", "-i", *map(str, cmake_files)])

    for command in commands:
        result = run(command, cwd=repo_root)
        if result.returncode != 0:
            return result.returncode
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="fail if any tracked source is unformatted")
    mode.add_argument("--fix", action="store_true", help="rewrite tracked source files in place")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    files = git_files(repo_root)
    clang_files, python_files, cmake_files = split_files(files)

    if require_tools(["clang-format", "black", "cmake-format"]) != 0:
        return 1

    if args.fix:
        return fix_files(repo_root, clang_files, python_files, cmake_files)

    status = 0
    status |= check_clang(repo_root, clang_files)
    status |= check_black(repo_root, python_files)
    status |= check_cmake(repo_root, cmake_files)
    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())

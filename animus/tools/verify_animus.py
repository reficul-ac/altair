#!/usr/bin/env python3
"""Configure, build, and test the standalone Animus tree."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
from pathlib import Path


def run_command(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print(f"+ {' '.join(command)}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def animus_root() -> Path:
    return Path(__file__).resolve().parents[1]


def conan_command(root: Path) -> str:
    local_conan = root / ".venv" / "bin" / "conan"
    if local_conan.exists():
        return str(local_conan)

    found = shutil.which("conan")
    if found:
        return found

    raise RuntimeError(
        "Conan is required. Install it on PATH or create animus/.venv with Conan."
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure, build, and test the independent Animus project."
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        help="Build directory. Defaults to <animus>/build.",
    )
    args = parser.parse_args()

    root = animus_root()
    build_dir = args.build_dir.resolve() if args.build_dir else root / "build"
    toolchain_file = build_dir / "conan_toolchain.cmake"
    conan = conan_command(root)
    conan_home = root / ".conan2"
    env = os.environ.copy()
    env["CONAN_HOME"] = str(conan_home)

    cache_file = build_dir / "CMakeCache.txt"
    if cache_file.exists():
        cache_text = cache_file.read_text(encoding="utf-8", errors="ignore")
        if str(toolchain_file) not in cache_text:
            shutil.rmtree(build_dir)

    if not (conan_home / "profiles" / "default").exists():
        run_command([conan, "profile", "detect", "--force"], cwd=root, env=env)

    run_command(
        [
            conan,
            "install",
            str(root),
            "-of",
            str(build_dir),
            "--build=missing",
            "-s",
            "build_type=Debug",
            "-s",
            "compiler.cppstd=20",
        ],
        cwd=root,
        env=env,
    )
    run_command(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}",
        ],
        cwd=root,
        env=env,
    )
    run_command(["cmake", "--build", str(build_dir)], cwd=root, env=env)
    run_command(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure"],
        cwd=root,
        env=env,
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

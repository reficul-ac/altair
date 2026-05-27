#!/usr/bin/env python3

import importlib.util
import sys
from pathlib import Path


def load_verify_agent_work(repo_root):
    path = repo_root / "tools/python/verify_agent_work.py"
    spec = importlib.util.spec_from_file_location("verify_agent_work", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_names(module, paths):
    return [check["name"] for check in module.select_verification_for_paths(paths)]


def expect_names(module, paths, expected):
    actual = check_names(module, paths)
    if actual != expected:
        print(f"paths: {paths}", file=sys.stderr)
        print(f"expected: {expected}", file=sys.stderr)
        print(f"actual: {actual}", file=sys.stderr)
        return 1
    return 0


def main():
    if len(sys.argv) != 2:
        print("usage: test_verify_agent_work.py <repo-root>", file=sys.stderr)
        return 2

    module = load_verify_agent_work(Path(sys.argv[1]))
    cases = (
        (["vehicle/altair_fsw.c"], ["format", "cmake", "release"]),
        (
            ["tests/integration/cruise6dof_case_initial.ini"],
            ["cmake", "sitl_plots", "sitl_live"],
        ),
        (["vehicle/mc_runner.c"], ["format", "cmake", "release", "mc"]),
        (["docs/testing.md"], ["format"]),
        (["docs/testing.md", "mixer/altair_mixer.c"], ["format", "cmake", "release"]),
    )
    status = 0
    for paths, expected in cases:
        status |= expect_names(module, paths, expected)
    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())

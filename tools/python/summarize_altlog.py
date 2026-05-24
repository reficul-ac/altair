#!/usr/bin/env python3
"""Emit deterministic summary JSON for an `.altlog` v1 bundle."""

from __future__ import annotations

import argparse

from altlog import load_altlog, write_summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="input .altlog directory or .zip")
    parser.add_argument("--output", required=True, help="output summary JSON")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    write_summary(load_altlog(args.input), args.output)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

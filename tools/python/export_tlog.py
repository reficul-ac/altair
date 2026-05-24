#!/usr/bin/env python3
"""Export captured MAVLink v1 frames from `.altlog` into a raw `.tlog` stream."""

from __future__ import annotations

import argparse

from altlog import load_altlog, write_tlog


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="input .altlog directory or .zip")
    parser.add_argument("--output", required=True, help="output .tlog path")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    count = write_tlog(load_altlog(args.input), args.output)
    print(f"wrote {args.output}: packets={count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

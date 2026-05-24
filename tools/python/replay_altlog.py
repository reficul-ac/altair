#!/usr/bin/env python3
"""Replay/export `.altlog` v1 bundles into deterministic artifacts."""

from __future__ import annotations

import argparse

from altlog import load_altlog, records_to_csv


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="input .altlog directory or .zip")
    parser.add_argument("--output-csv", required=True, help="exported replay CSV")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    bundle = load_altlog(args.input)
    records_to_csv(bundle, args.output_csv)
    print(f"wrote {args.output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

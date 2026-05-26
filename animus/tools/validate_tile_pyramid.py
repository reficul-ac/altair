#!/usr/bin/env python3
"""Validate an offline Animus XYZ terrain pack."""

from __future__ import annotations

import argparse
from pathlib import Path

from terrain_pack import add_manifest_argument, load_manifest, validate_pack


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_manifest_argument(parser)
    parser.add_argument("--pack-root", type=Path, required=True)
    args = parser.parse_args()

    errors = validate_pack(args.pack_root, load_manifest(args.manifest))
    if errors:
        for error in errors:
            print(error)
        return 1

    print(f"valid terrain pack: {args.pack_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


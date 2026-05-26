#!/usr/bin/env python3
"""Inspect one local Animus XYZ tile and print JSON metadata."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from terrain_pack import add_manifest_argument, inspect_tile, load_manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_manifest_argument(parser)
    parser.add_argument("--pack-root", type=Path, required=True)
    parser.add_argument("tile", type=Path, help="Tile path under --pack-root.")
    args = parser.parse_args()

    manifest = load_manifest(args.manifest) if args.manifest.exists() else None
    path = args.tile if args.tile.is_absolute() else args.pack_root / args.tile
    print(json.dumps(inspect_tile(args.pack_root, path, manifest), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

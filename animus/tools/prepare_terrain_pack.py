#!/usr/bin/env python3
"""Create or check an offline Animus terrain pack manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from terrain_pack import (
    add_manifest_argument,
    iter_tile_files,
    lake_tahoe_manifest,
    load_manifest,
    parse_tile_path,
    validate_manifest,
    write_manifest,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_manifest_argument(parser)
    parser.add_argument("--pack-root", type=Path, default=Path("animus/data/tiles/lake_tahoe"))
    parser.add_argument(
        "--init-lake-tahoe",
        action="store_true",
        help="Write the tracked Lake Tahoe Phase D manifest if it does not exist.",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List recognized tile files under --pack-root as JSON.",
    )
    args = parser.parse_args()

    if args.init_lake_tahoe:
        if args.manifest.exists():
            raise SystemExit(f"manifest already exists: {args.manifest}")
        write_manifest(args.manifest, lake_tahoe_manifest())
        print(args.manifest)
        return 0

    manifest = load_manifest(args.manifest)
    errors = validate_manifest(manifest)
    if errors:
        raise SystemExit("\n".join(errors))

    if args.list:
        tiles = []
        for path in iter_tile_files(args.pack_root):
            tile_path = parse_tile_path(args.pack_root, path)
            tiles.append(
                {
                    "layer": tile_path.layer,
                    "tile": tile_path.coord.key,
                    "extension": tile_path.extension,
                    "path": str(path),
                }
            )
        print(json.dumps({"tiles": tiles}, indent=2, sort_keys=True))
    else:
        print(json.dumps({"manifest": str(args.manifest), "pack_root": str(args.pack_root)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

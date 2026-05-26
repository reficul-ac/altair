#!/usr/bin/env python3
"""Download a PROJ geoid grid for explicit Animus datum correction."""

from __future__ import annotations

import argparse
import urllib.request
from pathlib import Path

DEFAULT_GRID = "us_nga_egm96_15.tif"
DEFAULT_BASE_URL = "https://cdn.proj.org"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--grid", default=DEFAULT_GRID, help="PROJ grid file name.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("animus/data/geoid"),
        help="Directory for downloaded grid files.",
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help="Base URL for PROJ-data grids.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print the planned download only.")
    args = parser.parse_args()

    output = args.output_dir / args.grid
    url = f"{args.base_url.rstrip('/')}/{args.grid}"
    if args.dry_run:
        print(f"{url} -> {output}")
        return 0

    args.output_dir.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        output.write_bytes(response.read())
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

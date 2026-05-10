#!/usr/bin/env python3
"""Print simple Monte Carlo summary statistics from mc_runner CSV output."""

import argparse
import csv
import statistics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path")
    args = parser.parse_args()

    with open(args.csv_path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        print("no rows")
        return 1

    airspeeds = [float(r["final_airspeed_mps"]) for r in rows]
    altitudes = [float(r["final_altitude_m"]) for r in rows]
    print(f"runs={len(rows)}")
    print(f"airspeed_mean={statistics.fmean(airspeeds):.6f}")
    print(f"altitude_mean={statistics.fmean(altitudes):.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

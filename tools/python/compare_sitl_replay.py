#!/usr/bin/env python3
"""Compare two SITL replay CSV files with numeric tolerances."""

import argparse
import csv
import math
import sys


def parse_args():
    parser = argparse.ArgumentParser(description="Compare SITL replay CSV files.")
    parser.add_argument("--expected", required=True, help="expected replay fixture CSV")
    parser.add_argument("--actual", required=True, help="actual replay CSV")
    parser.add_argument("--abs-tol", type=float, default=1.0e-5)
    parser.add_argument("--rel-tol", type=float, default=1.0e-6)
    return parser.parse_args()


def load_csv(path):
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path}: empty CSV")
    if len(rows) < 2:
        raise ValueError(f"{path}: CSV has no data rows")
    return rows[0], rows[1:]


def parse_number(path, row_number, column, value):
    try:
        parsed = float(value)
    except ValueError as exc:
        raise ValueError(
            f"{path}: row {row_number}, column {column}: not numeric: {value}"
        ) from exc
    if not math.isfinite(parsed):
        raise ValueError(f"{path}: row {row_number}, column {column}: non-finite value: {value}")
    return parsed


def compare_rows(expected_path, actual_path, header, expected_rows, actual_rows, abs_tol, rel_tol):
    for row_index, (expected_row, actual_row) in enumerate(
        zip(expected_rows, actual_rows), start=2
    ):
        if len(expected_row) != len(header):
            raise ValueError(
                f"{expected_path}: row {row_index}: expected {len(header)} fields, got {len(expected_row)}"
            )
        if len(actual_row) != len(header):
            raise ValueError(
                f"{actual_path}: row {row_index}: expected {len(header)} fields, got {len(actual_row)}"
            )

        for column_index, column in enumerate(header):
            expected_value = parse_number(
                expected_path, row_index, column, expected_row[column_index]
            )
            actual_value = parse_number(actual_path, row_index, column, actual_row[column_index])
            if not math.isclose(actual_value, expected_value, rel_tol=rel_tol, abs_tol=abs_tol):
                diff = actual_value - expected_value
                raise ValueError(
                    f"row {row_index}, column {column}: expected {expected_value:.12g}, "
                    f"actual {actual_value:.12g}, diff {diff:.12g}, "
                    f"abs_tol {abs_tol:.12g}, rel_tol {rel_tol:.12g}"
                )


def main():
    args = parse_args()
    try:
        expected_header, expected_rows = load_csv(args.expected)
        actual_header, actual_rows = load_csv(args.actual)
        if actual_header != expected_header:
            raise ValueError(
                "CSV headers differ:\n"
                f"expected: {','.join(expected_header)}\n"
                f"actual:   {','.join(actual_header)}"
            )
        if len(actual_rows) != len(expected_rows):
            raise ValueError(
                f"row count differs: expected {len(expected_rows)}, actual {len(actual_rows)}"
            )
        compare_rows(
            args.expected,
            args.actual,
            expected_header,
            expected_rows,
            actual_rows,
            args.abs_tol,
            args.rel_tol,
        )
    except (OSError, ValueError) as exc:
        print(f"compare_sitl_replay.py: {exc}", file=sys.stderr)
        return 1

    print(f"replay match: rows={len(expected_rows)} columns={len(expected_header)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

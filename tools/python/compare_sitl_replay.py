#!/usr/bin/env python3
"""Compare two SITL replay CSV files with numeric tolerances."""

import argparse
import json
import math
import sys

from sitl_csv import load_csv_rows, parse_finite_number


def parse_args():
    parser = argparse.ArgumentParser(description="Compare SITL replay CSV files.")
    parser.add_argument("--expected", required=True, help="expected replay fixture CSV")
    parser.add_argument("--actual", required=True, help="actual replay CSV")
    parser.add_argument("--abs-tol", type=float, default=1.0e-5)
    parser.add_argument("--rel-tol", type=float, default=1.0e-6)
    parser.add_argument("--summary-json", help="write deterministic comparison summary JSON")
    return parser.parse_args()


def parse_number(path, row_number, column, value):
    return parse_finite_number(value, f"{path}: row {row_number}, column {column}")


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
                return {
                    "row": row_index,
                    "column": column,
                    "expected": expected_value,
                    "actual": actual_value,
                    "diff": diff,
                    "absTol": abs_tol,
                    "relTol": rel_tol,
                }
    return None


def write_summary(
    path, status, header, expected_rows, actual_rows, first_mismatch=None, error=None
):
    summary = {
        "status": status,
        "columns": len(header) if header is not None else 0,
        "expectedRows": len(expected_rows) if expected_rows is not None else 0,
        "actualRows": len(actual_rows) if actual_rows is not None else 0,
        "firstMismatch": first_mismatch,
        "error": error,
    }
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, sort_keys=True)
        handle.write("\n")


def main():
    args = parse_args()
    expected_header = None
    expected_rows = None
    actual_rows = None
    first_mismatch = None
    try:
        expected_header, expected_rows = load_csv_rows(args.expected)
        actual_header, actual_rows = load_csv_rows(args.actual)
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
        first_mismatch = compare_rows(
            args.expected,
            args.actual,
            expected_header,
            expected_rows,
            actual_rows,
            args.abs_tol,
            args.rel_tol,
        )
        if first_mismatch is not None:
            raise ValueError(
                f"row {first_mismatch['row']}, column {first_mismatch['column']}: "
                f"expected {first_mismatch['expected']:.12g}, "
                f"actual {first_mismatch['actual']:.12g}, "
                f"diff {first_mismatch['diff']:.12g}, "
                f"abs_tol {args.abs_tol:.12g}, rel_tol {args.rel_tol:.12g}"
            )
    except (OSError, ValueError) as exc:
        if args.summary_json:
            write_summary(
                args.summary_json,
                "different",
                expected_header,
                expected_rows,
                actual_rows,
                first_mismatch=first_mismatch,
                error=str(exc),
            )
        print(f"compare_sitl_replay.py: {exc}", file=sys.stderr)
        return 1

    if args.summary_json:
        write_summary(args.summary_json, "match", expected_header, expected_rows, actual_rows)
    print(f"replay match: rows={len(expected_rows)} columns={len(expected_header)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

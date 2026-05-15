"""Shared CSV helpers for Altair SITL Python tools."""

import csv
import math


def load_dict_rows(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"{csv_path}: no data rows")
    return rows


def load_csv_rows(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        raise ValueError(f"{csv_path}: empty CSV")
    if len(rows) < 2:
        raise ValueError(f"{csv_path}: CSV has no data rows")
    return rows[0], rows[1:]


def parse_finite_number(value, context):
    try:
        parsed = float(value)
    except ValueError as exc:
        raise ValueError(f"{context}: not numeric: {value}") from exc
    if not math.isfinite(parsed):
        raise ValueError(f"{context}: non-finite value: {value}")
    return parsed


def finite_column(rows, column):
    values = []
    for index, row in enumerate(rows, start=2):
        if column not in row:
            raise ValueError(f"missing required column: {column}")
        values.append(parse_finite_number(row[column], f"row {index}, column {column}"))
    return values


def numeric_column(rows, column):
    values = []
    for index, row in enumerate(rows, start=2):
        if column not in row:
            raise ValueError(f"missing required column: {column}")
        try:
            value = float(row[column])
        except ValueError as exc:
            raise ValueError(f"row {index}: column {column} is not numeric: {row[column]}") from exc
        values.append(value)
    return values


def require_columns(rows, columns, label):
    available = set(rows[0].keys())
    missing = [column for column in columns if column not in available]
    if missing:
        raise ValueError(f"{label} requires missing column(s): {', '.join(missing)}")


def all_numeric_values_are_finite(rows):
    for row in rows:
        for value in row.values():
            try:
                parsed = float(value)
            except ValueError:
                continue
            if not math.isfinite(parsed):
                return False
    return True

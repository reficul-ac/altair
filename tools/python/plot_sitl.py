#!/usr/bin/env python3
"""Plot Altair SITL CSV logs."""

import argparse
import csv
import math
import sys
from pathlib import Path


SUPPORTED_PLOTS = ("velocities", "attitudes", "rates", "position", "ecef")


def parse_args():
    parser = argparse.ArgumentParser(description="Plot Altair SITL CSV logs.")
    parser.add_argument("csv_path")
    parser.add_argument("--plot", action="append", choices=SUPPORTED_PLOTS + ("all",), required=True)
    parser.add_argument("--out-dir")
    parser.add_argument("--show", action="store_true")
    return parser.parse_args()


def expand_plots(plot_names):
    expanded = []
    for name in plot_names:
        names = SUPPORTED_PLOTS if name == "all" else (name,)
        for expanded_name in names:
            if expanded_name not in expanded:
                expanded.append(expanded_name)
    return expanded


def load_rows(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
    if not rows:
        raise ValueError(f"{csv_path}: no data rows")
    return rows


def column_values(rows, column):
    values = []
    for index, row in enumerate(rows, start=2):
        try:
            value = float(row[column])
        except ValueError as exc:
            raise ValueError(f"row {index}: column {column} is not numeric: {row[column]}") from exc
        if not math.isfinite(value):
            raise ValueError(f"row {index}: column {column} is not finite: {row[column]}")
        values.append(value)
    return values


def require_columns(rows, columns, plot_name):
    available = set(rows[0].keys())
    missing = [column for column in columns if column not in available]
    if missing:
        raise ValueError(f"plot {plot_name} requires missing column(s): {', '.join(missing)}")


def save_figure(fig, out_dir, name):
    if out_dir is None:
        return
    out_path = Path(out_dir)
    out_path.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path / f"{name}.png", dpi=150)


def plot_three_axis(rows, pyplot, plot_name, columns, labels, title, out_dir):
    require_columns(rows, ("time_s",) + columns, plot_name)
    time_s = column_values(rows, "time_s")
    fig, axes = pyplot.subplots(3, 1, sharex=True, figsize=(9, 7))
    fig.suptitle(title)
    for axis, column, label in zip(axes, columns, labels):
        axis.plot(time_s, column_values(rows, column))
        axis.set_ylabel(label)
        axis.grid(True)
    axes[-1].set_xlabel("time_s")
    fig.tight_layout()
    save_figure(fig, out_dir, plot_name)
    return [fig]


def plot_position(rows, pyplot, out_dir):
    require_columns(rows, ("lat_deg", "lon_deg", "pos_n_m", "pos_e_m", "altitude_m"), "position")
    lat = column_values(rows, "lat_deg")
    lon = column_values(rows, "lon_deg")
    north = column_values(rows, "pos_n_m")
    east = column_values(rows, "pos_e_m")
    altitude = column_values(rows, "altitude_m")

    fig_ll, axis_ll = pyplot.subplots(figsize=(7, 6))
    axis_ll.plot(lon, lat)
    axis_ll.set_xlabel("lon_deg")
    axis_ll.set_ylabel("lat_deg")
    axis_ll.set_title("Latitude / Longitude")
    axis_ll.grid(True)
    fig_ll.tight_layout()
    save_figure(fig_ll, out_dir, "position_latlon")

    fig_3d = pyplot.figure(figsize=(8, 6))
    axis_3d = fig_3d.add_subplot(111, projection="3d")
    axis_3d.plot(east, north, altitude)
    axis_3d.set_xlabel("east_m")
    axis_3d.set_ylabel("north_m")
    axis_3d.set_zlabel("altitude_m")
    axis_3d.set_title("N/E/Altitude")
    fig_3d.tight_layout()
    save_figure(fig_3d, out_dir, "position_3d")
    return [fig_ll, fig_3d]


def plot_ecef(rows, pyplot, out_dir):
    columns = (
        "pos_ecef_x_m",
        "pos_ecef_y_m",
        "pos_ecef_z_m",
        "vel_ecef_x_mps",
        "vel_ecef_y_mps",
        "vel_ecef_z_mps",
    )
    if not all(column in rows[0] for column in columns):
        return []
    figures = []
    figures.extend(
        plot_three_axis(
            rows,
            pyplot,
            "ecef_position",
            columns[:3],
            ("x_m", "y_m", "z_m"),
            "ECEF Position",
            out_dir,
        )
    )
    figures.extend(
        plot_three_axis(
            rows,
            pyplot,
            "ecef_velocity",
            columns[3:],
            ("x_mps", "y_mps", "z_mps"),
            "ECEF Velocity",
            out_dir,
        )
    )
    return figures


def import_pyplot(show):
    try:
        import matplotlib
        if not show:
            matplotlib.use("Agg")
        import matplotlib.pyplot as pyplot
    except ImportError as exc:
        raise RuntimeError("matplotlib is required for plot_sitl.py") from exc
    return pyplot


def main():
    args = parse_args()
    if args.out_dir is None and not args.show:
        raise SystemExit("--out-dir or --show is required")
    try:
        rows = load_rows(args.csv_path)
        pyplot = import_pyplot(args.show)
        for plot_name in expand_plots(args.plot):
            if plot_name == "velocities":
                plot_three_axis(
                    rows,
                    pyplot,
                    "velocities",
                    ("vel_n_mps", "vel_e_mps", "vel_d_mps"),
                    ("north_mps", "east_mps", "down_mps"),
                    "NED Velocity",
                    args.out_dir,
                )
            elif plot_name == "attitudes":
                plot_three_axis(
                    rows,
                    pyplot,
                    "attitudes",
                    ("roll_rad", "pitch_rad", "yaw_rad"),
                    ("roll_rad", "pitch_rad", "yaw_rad"),
                    "Attitude",
                    args.out_dir,
                )
            elif plot_name == "rates":
                plot_three_axis(
                    rows,
                    pyplot,
                    "rates",
                    ("p_rps", "q_rps", "r_rps"),
                    ("p_rps", "q_rps", "r_rps"),
                    "Body Rates",
                    args.out_dir,
                )
            elif plot_name == "position":
                plot_position(rows, pyplot, args.out_dir)
            elif plot_name == "ecef":
                plot_ecef(rows, pyplot, args.out_dir)
        if args.show:
            pyplot.show()
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"plot_sitl.py: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

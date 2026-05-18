#!/usr/bin/env python3
"""Regression tests for Animus map-pack validation."""

from __future__ import annotations

import json
import shutil
import sqlite3
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "python"))

import validate_animus_map_pack as validator  # noqa: E402

PNG_1X1 = (
    b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01"
    b"\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01"
    b"\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82"
)


def write_pack_metadata(pack_dir: Path, source_status: str | None = "real-offline-imagery") -> None:
    imagery: dict[str, object] = {
        "format": "mbtiles",
        "path": "2d/imagery.mbtiles",
        "extension": "png",
    }
    if source_status is not None:
        imagery["sourceStatus"] = source_status
    metadata = {
        "schemaVersion": 1,
        "name": "Synthetic Stanford",
        "license": "test-license",
        "attribution": "test attribution",
        "minZoom": 12,
        "maxZoom": 13,
        "bounds": {"west": -122.25, "south": 37.36, "east": -122.05, "north": 37.50},
        "imagery": imagery,
    }
    (pack_dir / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
    (pack_dir / "attribution.txt").write_text("test attribution\n", encoding="utf-8")


def create_mbtiles(pack_dir: Path, *, full_coverage: bool) -> None:
    db_path = pack_dir / "2d" / "imagery.mbtiles"
    db_path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(db_path)
    try:
        connection.execute("CREATE TABLE metadata (name TEXT, value TEXT)")
        connection.execute(
            "CREATE TABLE tiles ("
            "zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)"
        )
        connection.executemany(
            "INSERT INTO metadata (name, value) VALUES (?, ?)",
            (
                ("name", "Synthetic Stanford"),
                ("format", "png"),
                ("attribution", "test attribution"),
            ),
        )
        bounds = {"west": -122.25, "south": 37.36, "east": -122.05, "north": 37.50}
        for zoom in range(12, 14):
            expected = validator._expected_tile_range(bounds, zoom)
            columns = range(expected["minX"], expected["maxX"] + 1)
            rows = range(expected["minY"], expected["maxY"] + 1)
            if not full_coverage:
                columns = range(expected["minX"], expected["minX"] + 1)
                rows = range(expected["minY"], expected["minY"] + 1)
            for column in columns:
                for row in rows:
                    tms_row = (1 << zoom) - 1 - row
                    connection.execute(
                        "INSERT INTO tiles "
                        "(zoom_level, tile_column, tile_row, tile_data) VALUES (?, ?, ?, ?)",
                        (zoom, column, tms_row, PNG_1X1),
                    )
        connection.commit()
    finally:
        connection.close()


def validate(pack_dir: Path, *, require_real_imagery: bool) -> list[str]:
    errors, _warnings = validator.validate_pack(
        pack_dir, require_3d=False, require_real_imagery=require_real_imagery
    )
    return errors


def test_placeholder_pack_fails_real_imagery() -> None:
    with TemporaryDirectory() as tmp:
        pack_dir = Path(tmp) / "pack"
        pack_dir.mkdir()
        write_pack_metadata(pack_dir, source_status="placeholder-center-tiles")
        create_mbtiles(pack_dir, full_coverage=True)
        errors = validate(pack_dir, require_real_imagery=True)
        assert any("sourceStatus" in error for error in errors), errors


def test_synthetic_full_coverage_passes_real_imagery() -> None:
    with TemporaryDirectory() as tmp:
        pack_dir = Path(tmp) / "pack"
        pack_dir.mkdir()
        write_pack_metadata(pack_dir)
        create_mbtiles(pack_dir, full_coverage=True)
        assert validate(pack_dir, require_real_imagery=True) == []


def test_sparse_coverage_fails_real_imagery() -> None:
    with TemporaryDirectory() as tmp:
        pack_dir = Path(tmp) / "pack"
        pack_dir.mkdir()
        write_pack_metadata(pack_dir)
        create_mbtiles(pack_dir, full_coverage=False)
        errors = validate(pack_dir, require_real_imagery=True)
        assert any("covers" in error for error in errors), errors


def test_missing_or_placeholder_source_status_fails_real_imagery() -> None:
    with TemporaryDirectory() as tmp:
        pack_dir = Path(tmp) / "pack"
        pack_dir.mkdir()
        write_pack_metadata(pack_dir, source_status=None)
        create_mbtiles(pack_dir, full_coverage=True)
        errors = validate(pack_dir, require_real_imagery=True)
        assert any("sourceStatus" in error for error in errors), errors

        replacement = Path(tmp) / "placeholder"
        shutil.copytree(pack_dir, replacement)
        write_pack_metadata(replacement, source_status="placeholder-center-tiles")
        errors = validate(replacement, require_real_imagery=True)
        assert any("sourceStatus" in error for error in errors), errors


def main() -> int:
    test_placeholder_pack_fails_real_imagery()
    test_synthetic_full_coverage_passes_real_imagery()
    test_sparse_coverage_fails_real_imagery()
    test_missing_or_placeholder_source_status_fails_real_imagery()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

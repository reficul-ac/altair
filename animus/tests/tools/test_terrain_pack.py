from __future__ import annotations

import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from terrain_pack import (  # noqa: E402
    TileCoord,
    inspect_float32,
    inspect_terrain_rgb,
    lake_tahoe_manifest,
    parse_tile_path,
    required_coords,
    validate_pack,
    web_mercator_bounds,
    write_manifest,
)
from download_lake_tahoe_pack import (  # noqa: E402
    ELEVATION_EXPORT_URL,
    IMAGERY_TILE_URL_TEMPLATE,
    PROVENANCE_SCHEMA,
    TOOL_VERSION,
    build_work_items,
    plan_json,
    provenance_json,
    terrain_rgb_height,
    terrain_rgb_triplet,
    tile_bounds_epsg3857,
)

try:
    from PIL import Image
except ImportError:  # pragma: no cover - verification environment should install Pillow.
    Image = None


@unittest.skipIf(Image is None, "Pillow is required for PNG terrain tool tests")
class TerrainPackTests(unittest.TestCase):
    def test_parse_tile_path_requires_xyz_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / "imagery" / "12" / "682" / "1563.png"
            path.parent.mkdir(parents=True)
            path.write_bytes(b"not decoded here")

            tile_path = parse_tile_path(root, path)

            self.assertEqual(tile_path.layer, "imagery")
            self.assertEqual(tile_path.coord, TileCoord(12, 682, 1563))
            self.assertEqual(tile_path.extension, "png")

            with self.assertRaisesRegex(ValueError, "unsupported layer"):
                parse_tile_path(root, root / "unknown" / "12" / "682" / "1563.png")

    def test_lake_tahoe_manifest_defines_expected_patch(self) -> None:
        coords = required_coords(lake_tahoe_manifest())

        self.assertEqual(len(coords), 9)
        self.assertIn(TileCoord(12, 682, 1563), coords)
        self.assertEqual(coords[0], TileCoord(12, 681, 1562))
        self.assertEqual(coords[-1], TileCoord(12, 683, 1564))

    def test_web_mercator_bounds_are_ordered(self) -> None:
        bounds = web_mercator_bounds(TileCoord(12, 682, 1563))

        self.assertLess(bounds["south_deg"], bounds["north_deg"])
        self.assertLess(bounds["west_deg"], bounds["east_deg"])
        self.assertGreater(bounds["north_deg"], 39.0)
        self.assertLess(bounds["west_deg"], -119.0)

    def test_epsg3857_bounds_for_tahoe_tile(self) -> None:
        west, south, east, north = tile_bounds_epsg3857(TileCoord(12, 682, 1563))

        self.assertAlmostEqual(west, -13364861.521606497)
        self.assertAlmostEqual(south, 4735426.77632324)
        self.assertAlmostEqual(east, -13355077.581985995)
        self.assertAlmostEqual(north, 4745210.715943743)
        self.assertLess(west, east)
        self.assertLess(south, north)

    def test_usgs_imagery_url_uses_arcgis_tile_order(self) -> None:
        root = Path("animus/data/tiles/lake_tahoe")
        item = build_work_items(lake_tahoe_manifest(), root)[4]

        self.assertEqual(item.coord, TileCoord(12, 682, 1563))
        self.assertEqual(
            item.imagery_url,
            "https://basemap.nationalmap.gov/arcgis/rest/services/"
            "USGSImageryOnly/MapServer/tile/12/1563/682",
        )
        self.assertEqual(
            item.imagery_output,
            root / "imagery" / "12" / "682" / "1563.png",
        )

    def test_terrain_rgb_round_trip(self) -> None:
        for meters in [-10.2, 0.0, 100.0, 1898.7, 2048.25]:
            red, green, blue = terrain_rgb_triplet(meters)

            self.assertAlmostEqual(terrain_rgb_height(red, green, blue), meters, places=1)

    def test_terrain_rgb_reports_meter_stats(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "height.png"
            image = Image.new("RGB", (2, 1))
            image.putdata([(1, 134, 160), (1, 138, 136)])
            image.save(path)

            details = inspect_terrain_rgb(path)

            self.assertEqual(details["width"], 2)
            self.assertEqual(details["height"], 1)
            self.assertAlmostEqual(details["min_m"], 0.0)
            self.assertAlmostEqual(details["max_m"], 100.0)

    def test_float32_stats_reject_bad_sample_count(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "height.f32"
            path.write_bytes(struct.pack("<4f", 1.0, 2.0, math.nan, 4.0))

            details = inspect_float32(path, tile_size=2)

            self.assertEqual(details["sample_count"], 4)
            self.assertEqual(details["min_m"], 1.0)
            self.assertEqual(details["max_m"], 4.0)

            with self.assertRaisesRegex(ValueError, "sample count"):
                inspect_float32(path, tile_size=3)

    def test_validate_pack_reports_missing_neighbors(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = lake_tahoe_manifest()
            manifest_path = root / "manifest.json"
            write_manifest(manifest_path, manifest)

            imagery = root / "pack" / "imagery" / "12" / "682"
            imagery.mkdir(parents=True)
            Image.new("RGB", (256, 256), (1, 2, 3)).save(imagery / "1563.png")

            elevation = root / "pack" / "elevation" / "12" / "682"
            elevation.mkdir(parents=True)
            Image.new("RGB", (256, 256), (1, 134, 160)).save(elevation / "1563.png")

            errors = validate_pack(root / "pack", manifest)

            self.assertGreaterEqual(len(errors), 16)
            self.assertTrue(
                any("missing required tile: imagery/12/681/1562.png" in error for error in errors)
            )
            self.assertTrue(
                any("missing required tile: elevation/12/683/1564.png" in error for error in errors)
            )

    def test_validate_pack_rejects_tiles_outside_manifest_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = lake_tahoe_manifest()
            imagery = root / "pack" / "imagery" / "12" / "700"
            imagery.mkdir(parents=True)
            Image.new("RGB", (256, 256), (1, 2, 3)).save(imagery / "1563.png")

            errors = validate_pack(root / "pack", manifest)

            self.assertTrue(
                any("tile is outside manifest coordinate coverage" in error for error in errors)
            )

    def test_validate_pack_inspects_present_optional_float32_tiles(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = lake_tahoe_manifest()
            bathymetry = root / "pack" / "bathymetry" / "12" / "682"
            bathymetry.mkdir(parents=True)
            (bathymetry / "1563.f32").write_bytes(struct.pack("<4f", 1.0, 2.0, 3.0, 4.0))

            errors = validate_pack(root / "pack", manifest)

            self.assertTrue(
                any("sample count 4 does not match 256x256" in error for error in errors)
            )

    def test_download_dry_run_plan_reports_expected_outputs(self) -> None:
        root = Path("animus/data/tiles/lake_tahoe")
        plan = plan_json(lake_tahoe_manifest(), root)

        self.assertEqual(plan["schema"], "animus.lake_tahoe_pack_plan.v1")
        self.assertEqual(plan["tool_version"], TOOL_VERSION)
        self.assertEqual(len(plan["imagery"]), 9)
        self.assertEqual(len(plan["elevation"]), 9)
        self.assertEqual(
            plan["imagery"][0]["output"],
            "animus/data/tiles/lake_tahoe/imagery/12/681/1562.png",
        )
        self.assertTrue(plan["imagery"][0]["url"].startswith("https://basemap.nationalmap.gov/"))
        self.assertIn(ELEVATION_EXPORT_URL, plan["elevation"][0]["url"])
        self.assertIn("bboxSR=3857", plan["elevation"][0]["url"])

    def test_provenance_json_shape(self) -> None:
        root = Path("animus/data/tiles/lake_tahoe")
        provenance = provenance_json(lake_tahoe_manifest(), root)

        self.assertEqual(provenance["schema"], PROVENANCE_SCHEMA)
        self.assertEqual(provenance["tool_version"], TOOL_VERSION)
        self.assertIn("created_utc", provenance)
        self.assertEqual(provenance["manifest"]["tile_set"]["zoom"], 12)
        self.assertEqual(
            provenance["sources"]["imagery"]["url_template"],
            IMAGERY_TILE_URL_TEMPLATE,
        )
        self.assertEqual(provenance["sources"]["elevation"]["url"], ELEVATION_EXPORT_URL)


if __name__ == "__main__":
    unittest.main()

# Animus Map Packs

Animus Qt uses operator-managed offline map packs under `map_packs/`. The
current runtime path is intentionally narrow: it discovers immediate child
directories, reads `metadata.json`, and renders local XYZ PNG imagery through
`image://animusTiles/<pack>/<z>/<x>/<y>`. Runtime map display must not download
tiles, process rasters, generate terrain, or block telemetry and SITL paths.

## Default SITL Stanford Pack

The default Altair SITL and mock telemetry origin is Stanford:

```text
center = 37.4275, -122.1697
bounds = west -122.25, south 37.36, east -122.05, north 37.50
zoom = 12-18
```

The checked-in `map_packs/default-sitl-stanford` pack is a lightweight
runtime-compatible seed for discovery, validation, and local UI checks. It is
not a full NAIP imagery bundle. Generate a full operational pack from local
source rasters with `tools/python/generate_animus_map_pack.py`.

## Current Runtime Layout

Use this layout for the current Qt app:

```text
map_packs/
  <pack_id>/
    metadata.json
    attribution.txt
    2d/
      xyz/
        <z>/<x>/<y>.png
```

`metadata.json` must use schema version 1, `imagery.format: "xyz"`, a relative
`imagery.tileRoot`, valid Web Mercator bounds, and a deterministic zoom range.
`terrain.format` should remain `"none"` until a real Cesium/WebEngine terrain
runtime exists.

MBTiles and PMTiles are future storage formats. Do not advertise them in
metadata until Animus has worker-backed SQLite or local-server tile reads for
that format. Existing PMTiles artifacts under `artifacts/` are not currently
discoverable by the runtime.

## Source Ranking

Prefer open and legally cacheable source data:

1. USGS/USDA NAIP imagery from The National Map or USGS NAIP ImageServer.
2. USGS 3DEP 1/3 arc-second DEM for staged terrain, hillshade, and contours.
3. Geofabrik California OSM extract only when road/label overlays are needed
   and ODbL attribution/share-alike obligations are acceptable.

Do not use Google Maps or Google Earth tiles for offline packs. Google map-tile
terms restrict unauthorized caching, storage, and offline use.

Useful source references:

- USGS National Map terms: <https://www.usgs.gov/faqs/what-are-terms-uselicensing-map-services-and-data-national-map>
- USGS 3DEP 1/3 arc-second DEM: <https://data.usgs.gov/datacatalog/data/USGS%3A3a81321b-c153-416f-98b7-cc8e5f0e17c3>
- USGS NAIP ImageServer: <https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/ImageServer>
- Geofabrik downloads and ODbL notes: <https://www.geofabrik.de/data/download.html>
- GDAL XYZ tile generation: <https://gdal.org/en/stable/programs/gdal2tiles.html>
- Google Map Tiles policy: <https://developers.google.com/maps/documentation/tile/policies>

## Attribution

USGS National Geospatial Program map services and data are public domain, and
USGS requests attribution. Include:

```text
Map services and data available from U.S. Geological Survey, National
Geospatial Program.
```

When OSM-derived data is included, also include:

```text
© OpenStreetMap contributors
```

## Generation

The generator does not download data. Download source imagery and DEM files
manually, then run:

```sh
python3 tools/python/generate_animus_map_pack.py \
  --pack-id default-sitl-stanford \
  --bbox -122.25 37.36 -122.05 37.50 \
  --min-zoom 12 \
  --max-zoom 18 \
  --imagery /path/to/naip_1.tif /path/to/naip_2.jp2 \
  --dem /path/to/usgs_3dep_dem.tif \
  --output-root map_packs
```

The imagery path crops/reprojects to EPSG:3857 with `gdalwarp`, then emits
north-origin XYZ tiles with `gdal2tiles.py --xyz`. If a DEM is supplied, the
tool stages the cropped DEM under `3d/terrain_dem/`, emits hillshade XYZ tiles,
and writes contour GeoJSON. The staged 3D files are developer artifacts for a
future Cesium path; the current runtime ignores them.

Validate a pack before using it:

```sh
python3 tools/python/validate_animus_map_pack.py map_packs/default-sitl-stanford
```

Use `--require-3d` only for future packs that are expected to include staged
DEM-derived topography.

## Future Runtime Work

Cesium terrain loading should use local HTTP URLs because Cesium terrain
providers expect URL-addressable `layer.json` and terrain tile resources. A
future local server should serve:

```text
/map_packs/<pack>/2d/xyz/{z}/{x}/{y}.png
/map_packs/<pack>/3d/hillshade_xyz/{z}/{x}/{y}.png
/map_packs/<pack>/3d/terrain_quantized_mesh/layer.json
/map_packs/<pack>/3d/terrain_quantized_mesh/{z}/{x}/{y}.terrain
```

Quantized-mesh generation remains a spike. Compare licensed MapTiler Terrain 3D
against maintained open DEM-to-quantized-mesh tooling, and defer to staged DEMs
if the mesh toolchain is not deterministic enough for Altair.

# Animus Terrain Data

Phase D defines the offline tile-pack shape used before runtime tile loading
exists. Phase H extends the same local XYZ layout with optional multi-zoom
`tile_sets` manifests for streamed terrain.

## Lake Tahoe Sample Area

The first sample area is a Lake Tahoe 3x3 tile patch:

- center: `39.0968, -120.0324`
- zoom: `12`
- center tile: `12/682/1563`
- patch: `x=681..683`, `y=1562..1564`
- manifest: `animus/data/sample_areas/lake_tahoe_phase_d.json`

The Phase H streaming sample keeps that Phase D `tile_set` for compatibility
and adds `tile_sets`:

- z11 parent coverage around `x=341 y=781`, 3x3
- z12 primary coverage around `x=682 y=1563`, 5x5
- z13 detailed center coverage around `x=1364 y=3126`, 6x6
- manifest: `animus/data/sample_areas/lake_tahoe_phase_h.json`

Real imagery, elevation, and bathymetry tiles remain local artifacts under
ignored paths such as `animus/data/tiles/` or `animus/data/downloaded/`.

The tracked Phase D manifest names the public USGS sources used by the local
preparation workflow:

- imagery: `USGSImageryOnly`, a cached 256x256 Web Mercator orthoimagery
  service at `https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer`
- elevation: USGS 3DEP bare-earth DEM exports from
  `https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer`

The workflow writes a generated provenance sidecar to
`animus/data/tiles/lake_tahoe/provenance.json`. The sidecar records source
URLs, the manifest tile range, creation timestamp, and tool/schema versions.

## XYZ Layout

Terrain packs use:

```text
<pack-root>/<layer>/<z>/<x>/<y>.<ext>
```

Supported Phase D layers are `imagery`, `elevation`, and `bathymetry`.

## Early Formats

- imagery: PNG RGB/RGBA tiles
- elevation: Terrain-RGB-compatible PNG tiles, decoded as meters
- bathymetry/elevation: little-endian raw float32 `.f32` tiles in meters

Each manifest layer declares tile size, sampling mode, no-data policy, source,
and format. Validators reject missing required tiles, unsupported formats,
wrong dimensions, malformed raw float32 data, non-finite height stats, and
missing required metadata.

## Tools

Install Python tool requirements when PNG inspection is needed:

```bash
python3 -m pip install -r animus/tools/requirements.txt
```

Useful commands:

```bash
python3 animus/tools/prepare_terrain_pack.py --manifest animus/data/sample_areas/lake_tahoe_phase_d.json
python3 animus/tools/download_lake_tahoe_pack.py --dry-run
python3 animus/tools/download_lake_tahoe_pack.py --manifest animus/data/sample_areas/lake_tahoe_phase_h.json --dry-run
python3 animus/tools/download_lake_tahoe_pack.py
python3 animus/tools/inspect_tile.py --pack-root animus/data/tiles/lake_tahoe imagery/12/682/1563.png
python3 animus/tools/validate_tile_pyramid.py --pack-root animus/data/tiles/lake_tahoe --manifest animus/data/sample_areas/lake_tahoe_phase_d.json
```

`download_lake_tahoe_pack.py` prepares the 18 required local artifacts for the
3x3 patch: 9 imagery PNGs under `imagery/12/<x>/<y>.png` and 9 Terrain-RGB
elevation PNGs under `elevation/12/<x>/<y>.png`. It requests USGS imagery tiles
with the ArcGIS cached service order `<z>/<y>/<x>`, converts USGS 3DEP DEM
exports to Terrain-RGB PNGs, rejects non-finite or no-data DEM pixels, writes
`provenance.json`, and then runs the same validation used by
`validate_tile_pyramid.py`.

After a successful real download, inspect the center tiles with:

```bash
python3 animus/tools/inspect_tile.py --pack-root animus/data/tiles/lake_tahoe --manifest animus/data/sample_areas/lake_tahoe_phase_d.json imagery/12/682/1563.png
python3 animus/tools/inspect_tile.py --pack-root animus/data/tiles/lake_tahoe --manifest animus/data/sample_areas/lake_tahoe_phase_d.json elevation/12/682/1563.png
```

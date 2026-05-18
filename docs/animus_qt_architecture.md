# Animus Qt Map And Terrain Architecture

Animus Qt is the canonical Altair ground-control shell. It owns the current Animus build, capture, telemetry, map, and operator workflows.

## Process Boundary

```text
PX4 SITL / simulator
  -> MAVLink UDP
  -> telemetry service
  -> latest-state and history buffers
  -> Qt model layer
  -> QML 2D map and WebEngine/Cesium 3D terrain
```

The map system is a passive consumer. Tile loading, terrain loading, network requests, disk IO, cache management, and rendering must not block MAVLink receive, telemetry decoding, SITL physics, or lockstep timing.

## Build

The Qt shell remains behind `ALTAIR_BUILD_ANIMUS_QT` so the default Altair C build stays usable on systems without Qt:

```sh
cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON
cmake --build build-animus-qt --parallel
ctest --test-dir build-animus-qt --output-on-failure -R animus_qt
```

Required Qt modules are Qt 6.4 or newer with Core, Gui, Qml, Quick, QuickControls2, Positioning, Network, WebChannel, WebEngineQuick, and Test. Ubuntu 24.04 packages do not provide the QtLocation runtime used by a future provider-backed map, so the current CI-safe 2D map is a strict-offline `QtQuick` renderer with vehicle, home, breadcrumb, provider, attribution, and offline-policy overlays. QtLocation-backed rendering remains a future runtime path, not a startup or screenshot dependency.

## Map Packs

The initial loader expects this shape:

```text
map_packs/
  <pack_id>/
    metadata.json
    attribution.txt
    2d/
      xyz/
        <z>/<x>/<y>.png
    3d/
      terrain_quantized_mesh/
      hillshade_xyz/
      contours/
```

Minimal `metadata.json`:

```json
{
  "schemaVersion": 1,
  "name": "Stanford Range",
  "license": "operator-managed",
  "attribution": "Operator-provided licensed imagery",
  "minZoom": 12,
  "maxZoom": 18,
  "bounds": {
    "west": -122.25,
    "south": 37.36,
    "east": -122.05,
    "north": 37.50
  },
  "imagery": {
    "format": "xyz",
    "tileRoot": "2d/xyz",
    "tileScheme": "xyz",
    "extension": "png"
  },
  "terrain": { "format": "none" }
}
```

`metadata.json` currently requires `schemaVersion: 1`, `name`, `license`,
`attribution`, `imagery.format: "xyz"`, valid `minZoom`/`maxZoom`, and a
relative local XYZ `imagery.tileRoot`. The current C++ loader rejects MBTiles,
PMTiles, and other imagery formats. Supported terrain metadata values are
`none` and `quantized-mesh`, but `has3dTerrain` is true only for
`terrain.format: "quantized-mesh"` with `3d/terrain/layer.json`; leave
`terrain.format` as `"none"` for staged DEM/topography packs until the Cesium
runtime path exists.

Future phases will extend validation for layer metadata, generated-at metadata,
MBTiles schema details, PMTiles support, and quantized-mesh terrain
completeness. See [Animus map packs](animus_map_packs.md) for the current
Stanford pack workflow and source policy.

## Current Scope

Implemented now:

- Qt/QML application under `tools/animus-qt`.
- QML workspace shell with 2D map, 3D terrain placeholder, and setup views.
- Deterministic Qt screenshot capture for `map-2d`, `terrain-3d`, and `setup` with mock telemetry and PNG nonblank checks.
- Vehicle state, bounded/decimated breadcrumb trail, map source registry, offline policy, map-pack discovery/validation, and Cesium WebChannel bridge.
- C++ MAVLink v1/v2 frame decode for heartbeat, attitude, global position, GPS raw, mission current, home position, and terrain report.
- UDP telemetry ingest with latest-value UI publication throttled to 1-30 Hz.
- Unit-test target for map policy, map-pack validation, bounded trails, MAVLink decode, and telemetry publication throttling when Qt is available.

Not implemented yet:

- MBTiles/PMTiles tile serving.
- QGC-derived provider/cache workers.
- Bundled CesiumJS vendor assets and quantized-mesh terrain loading.
- Mission, geofence, rally, and multi-vehicle model adapters.
- QtLocation-backed 2D map rendering once a portable install/runtime strategy exists.
- Broader semantic visual assertions beyond the current screenshot diagnostics.

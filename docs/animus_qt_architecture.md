# Animus Qt Map And Terrain Architecture

Animus Qt is an experimental ground-control shell that will coexist with the current Electron app until it is feature-complete enough to replace it.

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

The Qt shell is opt-in so the default Altair C build remains usable on systems without Qt:

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
    2d/
      imagery.mbtiles
    3d/
      terrain/
        layer.json
```

Minimal `metadata.json`:

```json
{
  "schemaVersion": 1,
  "name": "Stanford Range",
  "license": "operator-managed",
  "attribution": "Operator-provided licensed imagery",
  "imagery": { "format": "xyz" },
  "terrain": { "format": "quantized-mesh" }
}
```

`metadata.json` currently requires `schemaVersion: 1`, `name`, `license`, `attribution`, and `imagery.format`. Supported imagery formats are `xyz` and `mbtiles`. Supported terrain formats are `none` and `quantized-mesh`.

Future phases will extend validation for bounds, layers, versioning, generated-at metadata, MBTiles schema details, PMTiles support, and quantized-mesh terrain completeness.

## Current Scope

Implemented now:

- Opt-in Qt/QML application under `tools/animus-qt`.
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
- Rich overlap, clipping, and semantic visual assertions beyond nonblank screenshot checks.

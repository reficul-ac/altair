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

Required Qt modules are Qt 6.4 or newer with Core, Gui, Qml, Quick,
QuickControls2, Positioning, Network, WebChannel, WebEngineQuick, and Test.
Ubuntu 24.04 repositories available to this project do not provide Qt 6
Location, so the buildable 2D map surface remains QtQuick while the runtime
policy and offline tile-set state use the QGC-style cache manager.

## 2D Map Cache

The 2D map view uses an Altair QtQuick map surface with Altair-owned overlay
models for the selected vehicle, home, and breadcrumbs. Altair owns provider
policy, cache metadata, tile-set actions, and operator UI under
`tools/animus-qt/src/maps/qgc/`; telemetry and vehicle state remain outside the
map/cache layer. A real Altair-local QtLocation `QGroundControl` provider plugin
remains follow-up work.

The runtime cache root defaults to:

```text
map_cache/
  qgc_tile_cache.sqlite
  tiles/<provider>/<z>/<x>/<y>.png
```

The cache DB records offline tile-set metadata: provider id, bounds, zoom
range, estimated tile count, status, creation/update time, and last error. It
also records each expected tile state: queued, downloading, available, missing,
failed, retry count, last error, and update time. Startup seeds the default
cruise6dof five-mile offline area around the Stanford origin as metadata only;
fresh checkouts therefore show the schematic fallback with an empty-cache
status until the operator downloads or imports tiles.

The cache workflow exposes create, download, cancel, import, export, delete,
provider selection, cache status, and tile-count/size estimates. Provider
defaults are restricted to OpenStreetMap, the offline cache, and an operator URL
configured through `ANIMUS_QT_OPERATOR_TILE_URL`; credentialed or
ToS-sensitive providers are not enabled by default. Rendering uses local file
URLs only. Online map policy permits provider downloads into the cache, while
strict offline and cached/offline policies render only existing local tiles.

## Current Scope

Implemented now:

- Qt/QML application under `tools/animus-qt`.
- QML workspace shell with 2D map, 3D terrain placeholder, and setup views.
- Deterministic Qt screenshot capture for `map-2d`, `terrain-3d`, and `setup` with mock telemetry and PNG nonblank checks.
- Vehicle state, bounded/decimated breadcrumb trail, map provider registry,
  offline policy, QGC-style cache metadata/download manager, and Cesium
  WebChannel bridge.
- C++ MAVLink v1/v2 frame decode for heartbeat, attitude, global position, GPS raw, mission current, home position, and terrain report.
- UDP telemetry ingest with latest-value UI publication throttled to 1-30 Hz.
- Unit-test target for map policy, cache behavior, bounded trails, MAVLink decode, and telemetry publication throttling when Qt is available.

Not implemented yet:

- Directly vendored QGC provider/cache source; any future import must preserve
  upstream license headers and extend `docs/animus_qgc_map_audit.md`.
- Bundled CesiumJS vendor assets and quantized-mesh terrain loading.
- Mission, geofence, rally, and multi-vehicle model adapters.
- Broader semantic visual assertions beyond the current screenshot diagnostics.

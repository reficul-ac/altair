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
QuickControls2, Positioning, Network, WebEngineQuick, and Test.
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
- QML workspace shell with 2D map, WebEngine-backed 3D terrain, FPV, and
  tactical attitude workspaces, and setup views.
- Deterministic Qt screenshot capture for `map-2d`, `terrain-3d`, `fpv`,
  `tactical`, `setup`, and a seeded-cache Map 2D raster pass with mock
  telemetry, semantic tab diagnostics, and PNG nonblank checks.
- Vehicle state, bounded/decimated breadcrumb trail, map provider registry,
  offline policy, QGC-style cache metadata/download manager, and Cesium
  JavaScript bridge with snapshot and incremental terrain-scene updates.
- Bundled offline CesiumJS runtime assets loaded from Qt resources, with local
  quantized-mesh terrain selected from `map_cache/terrain/quantized-mesh` when
  `layer.json` is present and a deterministic Stanford/cruise6dof heightmap
  plus multi-level raster imagery fixture used as the fresh-checkout fallback.
- Terrain 3D vehicle rendering uses a bundled generic fixed-wing glTF model,
  altitude-aware trail segments, home marker primitives, and chase/orbit/free
  camera modes exposed through QML controls.
- FPV reuses the Terrain 3D Cesium/WebEngine terrain, imagery, selected GLB
  profile, and telemetry path, but hides the ownship and home/trail overlays
  while locking the camera to a nose/seeker forward view with a fixed 70 degree
  vertical FOV, modest default downward depression, and forward-hemisphere look
  clamp.
- Tactical attitude rendering reuses the Terrain 3D Cesium/WebEngine model
  profile, selected GLB, actuator mapping, polarity, and control-surface
  animation while locking camera interaction to rotate/zoom around the vehicle.
  The QML silhouette remains a degraded live fallback, but capture acceptance
  requires native `cesium-webengine` diagnostics with the selected GLB/profile
  and real control-surface pivot movement.
- Terrain 3D exports passive terrain-clearance analysis with current AGL,
  home-relative altitude, recent minimum/trend, terrain-report validity, and
  centralized `unknown`/`clear`/`caution`/`warning` thresholds.
- C++ MAVLink v1/v2 frame decode for heartbeat, attitude, global position, GPS raw, mission current, home position, and terrain report.
- UDP telemetry ingest with latest-value UI publication throttled to 1-30 Hz.
- Unit-test target for map policy, cache behavior, bounded trails, MAVLink decode, and telemetry publication throttling when Qt is available.

Not implemented yet:

- Directly vendored QGC provider/cache source; any future import must preserve
  upstream license headers and extend `docs/animus_qgc_map_audit.md`.
- Mission, geofence, rally, and multi-vehicle model adapters.
- Broader semantic visual assertions beyond the current screenshot diagnostics.

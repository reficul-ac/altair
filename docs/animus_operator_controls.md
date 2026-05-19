# Animus Operator Controls

The Altair Animus is a debugger-oriented MAVLink/SITL viewer. It is read-only by default. Vehicle-affecting actions are limited to guarded protocol operations when the selected link is explicitly writable, fresh, and the operator confirms the action.

## Launch

From the repository root:

```sh
cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON
cmake --build build-animus-qt --parallel
./build-animus-qt/tools/animus-qt/animus_qt
```

The live SITL bridge remains a separate Python process:

```sh
tools/python/run_sitl_session.py
```

`run_sitl_session.py` starts `mavlink_live_bridge.py`, optional QGroundControl forwarding, and realtime SITL. Animus clients consume decoded state from the bridge WebSocket. `--writable-animus` remains a SITL-only bridge advertisement for guarded write experiments.

To build, open Qt Animus, start UDP telemetry automatically, and run realtime
SITL into it:

```sh
tools/python/run_animus_sitl.py
```

The launcher builds `sitl_runner` and `animus_qt`, opens Animus with
`--start-udp-telemetry`, then runs `cruise6dof` SITL with MAVLink pointed at the
same UDP endpoint. Use `--skip-build` when both binaries are already current.

For a Python bridge session:

```sh
python3 tools/python/mavlink_live_bridge.py
```

It requires Qt 6.4 or newer with the Qt modules listed in [Animus Qt Map And Terrain Architecture](animus_qt_architecture.md).

## Verification Workflows

The visual verification workflow captures one screenshot per Qt workspace:

```sh
python3 tools/python/capture_animus_qt_sitl.py
```

The script captures `map-2d`, `terrain-3d`, and `setup` from the built Qt shell with mock telemetry. It writes PNGs, per-workspace logs, `visual-report.md`, and `run-manifest.json` under `artifacts/animus-qt-screenshots/<timestamp>/`. The report includes deterministic PNG diagnostics for capture size, toolbar/tab visibility, workspace content regions, sampled color diversity, and cross-workspace screenshot differences. The `terrain-3d` capture uses the bundled Cesium/WebEngine path and saves the native Cesium canvas PNG, with Xvfb WebGL flags supplied by the capture helper when no display is already present.

## Flight View

Flight View parity is future Qt Animus work. The current shell focuses on telemetry-backed setup, 2D map, and 3D terrain workspaces.

## Map View

- Drag the map to pan; panning leaves selected-vehicle follow and keeps a manual
  center until recentered.
- Mouse wheel and the `+`/`-` controls zoom the 2D map.
- `Snap` or the center pan-control recenters on the selected vehicle and resumes
  selected-vehicle follow after panning away.
- The 2D map overlay exposes provider/type selection, current offline policy,
  zoom, cache status, and a scale bar. Strict offline and cached/offline modes
  keep network-required providers disabled.
- Directional pan controls on the map provide mouse- and keyboard-friendly small
  moves without dragging.
- `Terrain 3D` opens the offline Cesium/WebEngine terrain path, using local
  quantized-mesh terrain when available and the bundled deterministic
  Stanford/cruise6dof heightmap fixture otherwise.
- Terrain 3D renders the selected vehicle as a bundled generic fixed-wing glTF
  model with altitude-aware breadcrumb history and chase, orbit, and free camera
  modes.
- Vehicle markers are triangular and point along velocity, with heading as a low-speed fallback. Home/origin markers use an X shape so they are visually distinct from vehicles.

Animus expects licensed QGC-style providers and operator-managed offline tile
caches. Repository artifacts do not ship generated topo, PMTiles, or broad
offline raster caches.

Animus does not scrape Google, Mapbox, Esri, or other tile services outside
their permitted offline or on-prem products. Operator-configured raster URLs are
disabled until `ANIMUS_QT_OPERATOR_TILE_URL` is set. If no active cache exists,
Animus shows an explicit offline/cache status while keeping vehicle overlays
usable.

Vehicle telemetry is used only for overlays: selected and fleet trails, event markers, an origin/home marker when available, mission waypoint paths, geofence polygons/circles, and rally points when decoded records are present. Terrain check requests are protocol-backed and appear in the operation history. Creating or editing geofences, rally points, or terrain tiles remains out of scope unless a decoded MAVLink path is added for that operation.

## Dashboard And Inspector

Dashboard and Inspector parity are future Qt Animus work. Keep any behavior still desired from the retired implementation in `TODO.md` instead of reintroducing stale UI code.

## Replay And Logs

- `Open` loads native Altair replay JSON.
- `Import` accepts native replay JSON plus CSV-style delimited logs and ULog-labeled imports that can be reduced to the currently supported deterministic replay frame fields.
- Native replay JSON is `type: "altair_session_replay"` with `schemaVersion: 1`, optional metadata/markers, and ordered or unordered frames containing `timestampS` plus a `session_snapshot`. Unsupported replay schema versions are rejected on import instead of being migrated implicitly.
- Replay and live WebSocket snapshots use tolerant v1 compatibility checks: required session fields (`vehicles`, `messages`, `events`, `packetCount`, `decodedCount`) and required vehicle basics must be present, while missing optional domains such as logs, console, command/audit state, protocol operations, and mock links normalize to empty arrays.
- Replay controls support pause/play, scrub, speed selection, reset, and marker navigation.
- Download/export parity is future Qt Animus work.
- Setup can request the onboard MAVLink log list, start per-log download operations, and erase onboard logs behind typed confirmation. Completed `LOG_DATA` downloads are assembled by byte offset and saved as raw `.bin` files under `path.join(app.getPath('userData'), 'onboard-logs')`; the saved path appears in the logs operation history. Imported replay behavior is unchanged.

## Analysis And GCS Parity

- Target multi-vehicle analysis count: 12 simultaneous vehicles. Above that, the viewer should still display fleet basics, but correlation, formation, and deconfliction inspection are optimized for the first 12 active vehicles.
- Analysis panels align streams by takeoff by default, expose formation offsets, and report minimum separation conflicts.
- The Plan workspace supports local waypoint list editing, local save/load, validation, MAVLink mission upload/download/clear, mission item request sequencing, ACK/status progress, and decoded mission/home overlays. Survey, corridor scan, structure scan, fixed-wing landing pattern, geofence editing, and rally editing remain outside the current editor.
- The Setup workspace shows readiness, preflight status, guarded Fly actions, parameter refresh/edit controls, onboard logs, operation history, and link diagnostics. Firmware setup, airframe selection, radio setup, sensor calibration, flight-mode edits, power setup, motor/actuator setup, safety edits, tuning, joystick setup, and persisted application settings remain outside the current setup editor.
- Command authority is normalized into explicit states: `read-only`, `sitl-writable`, `trusted-live-writable`, `maintenance-setup`, `unsupported`, or `unknown`. The default remains `read-only`; both writable modes require an explicit launch flag and current link guards.
- Firmware and mode display keeps both raw MAVLink values and normalized UI labels where supported. PX4 and ArduPilot heartbeat/custom modes have initial mappings; generic, unsupported, or unknown firmware states remain visible as explicit unsupported/unknown states instead of being treated as safe command capability.
- Readiness currently uses decoded link freshness, GPS fix, battery remaining, firmware identity, MAVLink system/failsafe state, and normalized mission state/progress. Estimator health, power-domain checks, firmware-specific preflight checks, and field-operation acceptance remain roadmap work.
- The Video workspace lists advertised camera or camera-metadata records and exposes guarded MAVLink still capture, recording start/stop, zoom, and focus commands when a writable authority and camera metadata are present. Video display, new RTP/RTSP/UVC playback dependencies, map/video switching, and telemetry subtitle export are still out of scope.
- Guarded command buttons are command stubs for SITL-only safety experiments. High-consequence commands require typed confirmation, altitude commands require typed target altitude, and failed or timed-out command transactions can be manually retried after current guards are re-evaluated. Full GCS command forms, firmware capability discovery, and live-vehicle write authority remain roadmap work.
- MAVLink console access is intentionally limited to diagnostics, captured status text, operation history, and audit trails in this debugger-oriented viewer. Raw command console writes are out of scope unless a future operator safety review approves them.

## Live SITL Swarms

Single vehicle:

```sh
tools/python/run_sitl_session.py --duration 30
tools/python/run_animus_sitl.py --duration 30
```

Three vehicles with predictable IDs and source ports:

```sh
tools/python/run_sitl_session.py --vehicles 3 --system-id-base 21 --mavlink-port-base 14700 --duration 30
```

The launcher creates per-vehicle output files such as `sitl_live_sys21.csv`, assigns MAVLink system IDs `21..23`, binds local MAVLink source ports `14700..14702`, forwards every instance to the viewer bridge, and keeps QGroundControl forwarding on unless `--no-qgc` is passed.

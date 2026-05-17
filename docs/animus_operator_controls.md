# Animus Operator Controls

The Altair Animus is a debugger-oriented MAVLink/SITL viewer. It is read-only by default. Vehicle-affecting actions are limited to guarded protocol operations when the selected link is explicitly writable, fresh, and the operator confirms the action.

## Launch

From the repository root:

```sh
npm run dev --prefix tools/animus
```

For the Electron app:

```sh
npm run app --prefix tools/animus
```

Writable launches are opt-in:

```sh
npm run app --prefix tools/animus -- --writable-animus
npm run app --prefix tools/animus -- --trusted-live-writable
```

`--writable-animus` is intended for SITL. `--trusted-live-writable` exposes live-vehicle protocol writes only after an operator intentionally chooses that authority mode at launch; Animus still displays endpoint, QGC forwarding, duplicate-GCS risk, and MAVLink signing status in Setup.

For a Python bridge session:

```sh
python3 tools/python/mavlink_live_bridge.py
```

## Verification Workflows

The lightweight visual workflow launches live SITL, opens Animus, and captures one screenshot per workspace:

```sh
python3 tools/python/capture_animus_sitl.py
```

The screenshot artifact directory includes `visual-report.md`, which summarizes live telemetry, requested workspaces and viewports, screenshot diagnostics, warning notes, and links to the manifests and service logs.

The deeper interaction workflow launches the same live stack and drives Chromium through Playwright:

```sh
python3 tools/python/interact_animus_sitl.py
```

Use the interaction harness when changing workspace controls, dashboard widget workflows, guarded command surfaces, mission editing, replay/session controls, or any UI behavior that needs screenshots at specific checkpoints. It writes logs, the Playwright report, and checkpoint screenshots under `artifacts/animus-interactions/<timestamp>/`.

## Flight View

- Camera buttons select `Chase`, `Orbit`, `Top`, `Side`, or `Free`.
- `C` cycles the camera mode.
- Free camera keeps the existing mouse-look and keyboard controls: drag to look, `W/A/S/D` to move, `Q/E` down/up, and `Shift` for faster movement.
- `O` toggles the orthographic trail inset.
- `V` cycles the visual theme.
- `M` adds a local debug marker when the Electron service is available.
- The Flight HUD compass is ground-track primary. It derives direction from
  north/east velocity when the vehicle is moving, falls back to decoded MAVLink
  heading when track is unavailable, and finally falls back to yaw-derived
  heading.

Vehicle meshes are chosen from heartbeat vehicle type: fixed-wing, multirotor, VTOL/tailsitter, or generic MAVLink.

## Map View

- Drag the map to pan.
- Mouse wheel zooms around the cursor.
- `Focus` recenters on the selected vehicle and resumes selected-vehicle follow.
- `+` and `-` adjust zoom.
- `Satellite` shows the active offline satellite tile cache through MapLibre GL JS.
- `Terrain 3D` is enabled when an active offline DEM cache exists. It keeps vehicle overlays usable when satellite imagery is missing and shows shaded terrain until an active satellite cache can be draped over the Flight View terrain mesh.

Animus expects operator-provided, licensed XYZ raster tile URL templates with `{z}`, `{x}`, and `{y}` placeholders and any API key embedded in the query string. Setup estimates the tile count for the current Map viewport, or the default SITL origin at `37.4275, -122.1697` when no map viewport is available, then caches satellite tiles under Electron user data at `map-cache/tiles/<setId>/<z>/<x>/<y>.<ext>` and DEM tiles under `map-cache/dem/<setId>/<z>/<x>/<y>.<ext>`. The satellite cache index is stored at `map-cache/index.json`; the DEM cache index is stored at `map-cache/dem-index.json`. Repository artifacts no longer ship generated topo or PMTiles fallbacks.

Animus downloads only from `http` or `https` XYZ templates that the operator is licensed to cache. It does not scrape Google, Mapbox, Esri, or other tile services outside their permitted offline or on-prem products. DEM cache setup supports MapLibre-compatible RGB DEM encodings: `terrarium` and `mapbox`. If no active cache exists, or the current view requests missing tiles, Animus shows an explicit offline cache status while keeping vehicle overlays usable.

Vehicle telemetry is used only for overlays: selected and fleet trails, event markers, an origin/home marker when available, mission waypoint paths, geofence polygons/circles, and rally points when decoded records are present. Terrain check requests are protocol-backed and appear in the operation history. Creating or editing geofences, rally points, or terrain tiles remains out of scope unless a decoded MAVLink path is added for that operation.

## Dashboard View

- `Add Widget` opens the built-in widget catalog for fixed-grid status and guarded-control widgets.
- Each widget can be dragged within the grid, resized between compact, wide, and full spans, or removed; `Reset Layout` restores the default dashboard.
- `Import` and `Export` share dashboard profiles as the same JSON layout shape persisted in `dashboard-layout.json`: `schemaVersion: 1` plus a `widgets` array. Import replaces the active layout after normalizing invalid, duplicate, or unsupported widget entries.
- Threshold customization is intentionally out of scope.
- The Electron app persists the layout as JSON at `path.join(app.getPath('userData'), 'dashboard-layout.json')`. Missing or invalid settings fall back to the default layout without overwriting the file until the operator changes or resets the layout.
- Application settings include the default offline map style (`mapStyle: "satellite"`), whether the map should follow the selected vehicle (`mapFollowSelected`), licensed satellite and DEM XYZ tile templates, attribution, DEM encoding, active cache set ids, zoom defaults, and max tile count guards.

Status widgets read the same session snapshot used by Flight, Map, Inspector, and Setup. The guarded control widget uses the existing command authority, confirmation, dispatch, audit, retry, and guard evaluation path. Commands are disabled when the selected vehicle reports read-only authority, stale or non-live link state, unsupported command capability, or a decoded blocked-command reason.

## Inspector View

- Use the message filter to narrow by message name, message id, or `system:component`.
- Select a message row to inspect its latest decoded fields.
- Click one or more numeric field buttons to overlay chart traces.
- `Export CSV` downloads the currently selected chart samples for the active message.
- `Export Log` downloads the continuous browser-side inspector CSV log across vehicles, messages, and fields.
- The Compare panel keeps per-vehicle streams visible while synchronized replay inspection is active.

## Replay And Logs

- `Open` loads native Altair replay JSON.
- `Import` accepts native replay JSON plus CSV-style delimited logs and ULog-labeled imports that can be reduced to the currently supported deterministic replay frame fields.
- Native replay JSON is `type: "altair_session_replay"` with `schemaVersion: 1`, optional metadata/markers, and ordered or unordered frames containing `timestampS` plus a `session_snapshot`. Unsupported replay schema versions are rejected on import instead of being migrated implicitly.
- Replay and live WebSocket snapshots use tolerant v1 compatibility checks: required session fields (`vehicles`, `messages`, `events`, `packetCount`, `decodedCount`) and required vehicle basics must be present, while missing optional domains such as logs, console, command/audit state, protocol operations, and mock links normalize to empty arrays.
- Replay controls support pause/play, scrub, speed selection, reset, and marker navigation.
- `Download` saves the current replay/session metadata path exposed by Electron.
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
```

Three vehicles with predictable IDs and source ports:

```sh
tools/python/run_sitl_session.py --vehicles 3 --system-id-base 21 --mavlink-port-base 14700 --duration 30
```

The launcher creates per-vehicle output files such as `sitl_live_sys21.csv`, assigns MAVLink system IDs `21..23`, binds local MAVLink source ports `14700..14702`, forwards every instance to the viewer bridge, and keeps QGroundControl forwarding on unless `--no-qgc` is passed.

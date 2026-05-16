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

## Flight View

- Camera buttons select `Chase`, `Orbit`, `Top`, `Side`, or `Free`.
- `C` cycles the camera mode.
- Free camera keeps the existing mouse-look and keyboard controls: drag to look, `W/A/S/D` to move, `Q/E` down/up, and `Shift` for faster movement.
- `O` toggles the orthographic trail inset.
- `V` cycles the visual theme.
- `M` adds a local debug marker when the Electron service is available.

Vehicle meshes are chosen from heartbeat vehicle type: fixed-wing, multirotor, VTOL/tailsitter, or generic MAVLink.

## Map View

- Drag the map to pan.
- Mouse wheel zooms around the cursor.
- `Focus` recenters on the selected vehicle and resumes selected-vehicle follow.
- `+` and `-` adjust zoom.

The map renders selected and fleet trails, event markers, an origin/home marker when available, mission waypoint paths, terrain reports, geofence polygons/circles, and rally points when decoded records are present. Terrain check requests are protocol-backed and appear in the operation history. Creating or editing geofences, rally points, or terrain tiles remains out of scope unless a decoded MAVLink path is added for that operation.

## Dashboard View

- `Add Widget` opens the built-in widget catalog for fixed-grid status and guarded-control widgets.
- Each widget can be dragged within the grid, resized between compact, wide, and full spans, or removed; `Reset Layout` restores the default dashboard.
- `Import` and `Export` share dashboard profiles as the same JSON layout shape persisted in `dashboard-layout.json`: `schemaVersion: 1` plus a `widgets` array. Import replaces the active layout after normalizing invalid, duplicate, or unsupported widget entries.
- Threshold customization is intentionally out of scope.
- The Electron app persists the layout as JSON at `path.join(app.getPath('userData'), 'dashboard-layout.json')`. Missing or invalid settings fall back to the default layout without overwriting the file until the operator changes or resets the layout.

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
- Replay controls support pause/play, scrub, speed selection, reset, and marker navigation.
- `Download` saves the current replay/session metadata path exposed by Electron.
- Setup can request the onboard MAVLink log list, start per-log download operations, and erase onboard logs behind typed confirmation. Progress is reported by operation id and received-byte counters from `LOG_DATA`; imported replay behavior is unchanged.

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

# Animus Operator Controls

The Altair Animus is a debugger-oriented MAVLink/SITL viewer. It is read-only by default. Vehicle-affecting actions are limited to guarded SITL command controls and simple waypoint mission upload packet emission when the selected link is explicitly writable, fresh, and the operator confirms the action.

## Launch

From the repository root:

```sh
npm run dev --prefix tools/animus
```

For the Electron app:

```sh
npm run app --prefix tools/animus
```

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

The map renders selected and fleet trails, event markers, an origin/home marker when available, and mission waypoint paths when a session snapshot includes waypoint coordinates.
It can render geofence polygons/circles and rally points when those records are already present in the session snapshot. Creating or editing geofences and rally points is not implemented. The offline grid map remains available for replay and mock-link testing without hardware or SITL.

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
- `Download` saves the current replay/session metadata path exposed by Electron. Onboard MAVLink log listing, download, deletion, and `.tlog` recording are roadmap items.

## Analysis And GCS Parity

- Target multi-vehicle analysis count: 12 simultaneous vehicles. Above that, the viewer should still display fleet basics, but correlation, formation, and deconfliction inspection are optimized for the first 12 active vehicles.
- Analysis panels align streams by takeoff by default, expose formation offsets, and report minimum separation conflicts.
- The Plan workspace supports local waypoint list editing, local save/load, validation, guarded SITL waypoint upload packet emission, and read-only mission progress from decoded MAVLink mission status. Survey, corridor scan, structure scan, fixed-wing landing pattern, geofence editing, rally editing, full mission download, full mission ACK synchronization, and terrain-following workflows are disabled roadmap surfaces. Mission request/item/ACK, terrain, and home-position messages are decoded for inspection when present.
- The Setup workspace shows readiness, preflight status, guarded SITL Fly actions, parameter values, and link diagnostics. Parameter rows are inspect-only: parameter fetch/cache/edit/validate/upload, firmware setup, airframe selection, radio setup, sensor calibration, flight-mode edits, power setup, motor/actuator setup, safety edits, tuning, camera setup, joystick setup, and persisted application settings are disabled roadmap surfaces. Parameter, camera-metadata, and log-list/download messages are decoded for inspection when present.
- Command authority is normalized into explicit states: `read-only`, `sitl-writable`, `trusted-live-writable`, `maintenance-setup`, `unsupported`, or `unknown`. Current writable behavior is limited to `sitl-writable`; trusted live-link and maintenance/setup authority remain disabled roadmap states.
- Firmware and mode display keeps both raw MAVLink values and normalized UI labels where supported. PX4 and ArduPilot heartbeat/custom modes have initial mappings; generic, unsupported, or unknown firmware states remain visible as explicit unsupported/unknown states instead of being treated as safe command capability.
- Readiness currently uses decoded link freshness, GPS fix, battery remaining, firmware identity, MAVLink system/failsafe state, and normalized mission state/progress. Estimator health, power-domain checks, firmware-specific preflight checks, and field-operation acceptance remain roadmap work.
- The Video workspace lists advertised camera or camera-metadata records for inspection. Video display, real RTP/RTSP/UVC stream plumbing, MAVLink camera settings, still capture, local recording, map/video switching, and telemetry subtitle export are disabled until protocol-backed stream support exists.
- Guarded command buttons are command stubs for SITL-only safety experiments. High-consequence commands require typed confirmation, altitude commands require typed target altitude, and failed or timed-out command transactions can be manually retried after current guards are re-evaluated. Full GCS command forms, firmware capability discovery, and live-vehicle write authority remain roadmap work.
- MAVLink console access is intentionally limited to diagnostics and captured status text in this debugger-oriented viewer. Raw command console writes are out of scope unless a future operator safety review approves them.

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

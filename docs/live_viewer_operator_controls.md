# Live Viewer Operator Controls

The Altair live viewer is read-only by default. Vehicle-affecting actions are exposed only through guarded command controls: the selected live link must advertise the capability, the link must be writable, and the operator must confirm the action.

## Launch

From the repository root:

```sh
npm run dev --prefix tools/live_viewer
```

For the Electron app:

```sh
npm run app --prefix tools/live_viewer
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

The map renders selected and fleet trails, event markers, an origin/home marker when available, and mission waypoint paths when future MAVLink session snapshots include waypoint coordinates.
It also renders geofence polygons/circles and rally points when those records are present in the session snapshot. The offline grid map remains available for replay and mock-link testing without hardware or SITL.

## Inspector View

- Use the message filter to narrow by message name, message id, or `system:component`.
- Select a message row to inspect its latest decoded fields.
- Click one or more numeric field buttons to overlay chart traces.
- `Export CSV` downloads the currently selected chart samples for the active message.
- `Export Log` downloads the continuous browser-side inspector CSV log across vehicles, messages, and fields.
- The Compare panel keeps per-vehicle streams visible while synchronized replay inspection is active.

## Replay And Logs

- `Open` loads native Altair replay JSON.
- `Import` accepts native replay JSON plus ULog/CSV-style delimited logs and converts them to deterministic replay frames with source metadata.
- Replay controls support pause/play, scrub, speed selection, reset, and marker navigation.
- `Download` saves the current replay/session metadata path exposed by Electron.

## Analysis And GCS Parity

- Target multi-vehicle analysis count: 12 simultaneous vehicles. Above that, the viewer should still display fleet basics, but correlation, formation, and deconfliction inspection are optimized for the first 12 active vehicles.
- Analysis panels align streams by takeoff by default, expose formation offsets, and report minimum separation conflicts.
- The Plan workspace shows mission items, geofences, rally points, and placeholders for survey, corridor scan, structure scan, and fixed-wing landing pattern workflows. Upload/download vehicle writes remain disabled until a writable command-capable link is present.
- The Setup workspace shows readiness, preflight checks, guarded Fly actions, parameters, and link diagnostics. Firmware, airframe, radio, sensors, flight modes, power, motors, safety, tuning, camera, joystick, and application settings are represented as inspectable surfaces; unsupported write actions stay disabled.
- The Video workspace lists RTP, RTSP, UVC, and MAVLink camera metadata streams when advertised. Capture, local recording, MAVLink camera settings, map/video switching, and telemetry subtitle export controls are present but stay unavailable until a stream advertises support.
- MAVLink console access is intentionally limited to diagnostics and captured status text in this debugger-oriented viewer. Raw command console writes remain out of scope unless a future operator safety review approves them.

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

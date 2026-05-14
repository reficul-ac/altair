# Live Viewer Operator Controls

The Altair live viewer is a read-only MAVLink debugging surface. It does not send vehicle commands.

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

## Inspector View

- Use the message filter to narrow by message name, message id, or `system:component`.
- Select a message row to inspect its latest decoded fields.
- Click one or more numeric field buttons to overlay chart traces.
- `Export CSV` downloads the currently selected chart samples for the active message.

## Replay Placeholders

ULog import, deterministic replay timeline controls, video sync, live command actions, and full QGroundControl Plan/Fly setup surfaces are intentionally deferred until the viewer has a stronger replay/session foundation.

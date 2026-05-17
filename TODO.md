# Altair Active Backlog

This file tracks near-term repository work. Roadmap and design context live in:

- [Architecture](docs/architecture.md)
- [Vehicle layer](docs/vehicle_layer.md)
- [Simulation and Monte Carlo](docs/simulation_and_mc.md)
- [Testing](docs/testing.md)
- [Animus operator controls](docs/animus_operator_controls.md)

Reusable Bayek framework follow-up is tracked separately in
[`bayek/TODO.md`](bayek/TODO.md).

## Baseline Health

- [ ] Keep CI green for formatting, Debug/Release CMake, CTest, and Animus.
- [ ] Keep generated build, CSV, plot, and Animus distribution artifacts out of the worktree.
- [ ] Periodically refresh Animus dependencies on the Node 24 LTS path and require `npm audit --audit-level=moderate` to pass.

## Vehicle And SITL

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Document mass, wing area, speed limits, control limits, safe actuator positions, and aero data provenance.
- [ ] Expand mixer tests for edge cases, saturation, sign conventions, and failsafe/disarmed outputs.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Wire Bayek wind, turbulence, sensor noise, and bias hooks into Altair SITL
  and Monte Carlo scenarios after the reusable hooks exist.
- [ ] Add randomized Monte Carlo inputs for Altair-owned dispersions, guardrails,
  and summary CSV semantics.
- [ ] Document Altair aero data provenance before replacing scalar aero formulas
  with reusable Bayek aero database helpers.

## Telemetry And Replay

- [ ] Define required telemetry packets for SITL, embedded, and log replay.
- [ ] Add Altair replay schema compatibility checks around imported telemetry
  and live viewer session snapshots.
- [ ] Add log-to-replay tooling for deterministic regression tests.

## Animus

- [x] Add a Playwright-backed live SITL interaction harness for Animus workspace controls and checkpoint screenshots.
- [x] Fix live capture flight workspace rendering so the 3D scene is nonblank under `python3 tools/python/capture_animus_sitl.py`.
- [x] Resolve flight workspace topbar/status-strip overlap at the default `1440x900` capture viewport.
- [x] Populate map and inspector live workspaces during SITL capture from browser bridge telemetry.
- [x] Add firmware-specific readiness checks beyond generic MAVLink telemetry.
- [x] Make emergency action UX, command cancellation, stale-link protection, and duplicate-GCS handling explicit.
- [x] Add durable command audit basics with command payload, confirmation, ACK/NACK, timeout, cancellation, and failure reason.
- [x] Add MAVLink v2/signing awareness to live diagnostics.
- [x] Add retry dispatch and retry audit semantics for live commands.
- [x] Expand durable command audit history with operator and session identity.
- [x] Add broader dialect-driven MAVLink decoding beyond the currently supported message set.
- [x] Add typed command workflows for high-consequence live actions.
- [x] Add richer protocol-backed GCS workflows for parameter editing, mission transfer, map/terrain, log, and camera operations.
- [x] Add dashboard drag-and-drop widget reordering and per-widget resizing.
- [x] Add dashboard profile import/export for sharing operator layouts.
- [x] Add dashboard widget groups or presets for flight test, mission planning, and maintenance workflows.
- [x] Add richer dashboard widget configuration such as thresholds, units, and selected vehicle/fleet scoping.
- [x] Add persisted application settings beyond the dashboard layout.
- [ ] Add real 2D and 3D terrain map views in Animus for clearer terrain and
  vehicle-position visualization.
- [x] Correct the flight-view 3D aircraft model heading so the nose points in
  the vehicle's telemetry-derived direction of travel.
- [x] Verify and fix 3D aircraft roll, pitch, and yaw rendering so the model
  attitude follows telemetry correctly across all axes.
- [x] Add finer 3D aircraft model details, including visible actuator surfaces
  that deflect from live actuator or control telemetry.
- [x] Rearrange the main flight-tab status strip so longer status labels remain
  fully readable at the default capture viewport.
- [x] Add an FPV camera preset alongside Chase, Orbit, Top, Side, and Free.
- [x] Make camera preset views snap to useful positions without locking normal
  rotate and zoom controls by default.
- [x] Add an explicit camera-view lock option near the view controls, defaulting
  off, to enforce fixed preset views when desired.
- [x] Rework tactical attitude rings into three dimensional gyro-style rings
  around the aircraft that track roll, pitch, and yaw with vehicle motion.
- [x] Fix the Electron screenshot capture hang so `capture_animus_sitl.py`
  produces screenshots reliably instead of relying on timeout failure.
- [x] Decide whether Animus capture artifacts should represent the requested
  outer viewport or Electron web contents size; current `1440x900` captures
  save `1440x835` PNGs.
- [x] Reduce unnecessary vertical scrollbars in default Animus `1440x900`
  capture views, especially the live debugger pane and longer workspace panels.
- [x] Fix narrow `390x844` Animus workspace navigation so the tab row does not
  create horizontal overflow across live workspaces.
- [x] Fix narrow `390x844` flight workspace control wrapping so camera/HUD
  controls do not overlap telemetry cards and the lower scene HUD.
- [x] Fix narrow `390x844` inspector message table overflow so message names,
  rates, and counts remain readable without clipped columns.
- [x] Recenter the `1440x900` flight Free camera state so the vehicle remains
  visible instead of rendering off-canvas or clipped into the top status area.
- [ ] Persist completed onboard MAVLink log downloads as raw `.tlog` or `.bin`
  files once `LOG_DATA` byte assembly is promoted from operation progress to
  durable file output.

## Test Ownership

- [ ] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, telemetry integration, and Animus.
- [ ] Keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [ ] Add path-aware verification selection so agent and human checks can map changed files to the smallest defensible command set.
- [ ] Build richer visual verification reports that combine metrics, plots, screenshots, logs, and manifests in one reviewable artifact.

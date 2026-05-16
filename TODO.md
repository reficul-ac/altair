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
- [ ] Add dashboard widget groups or presets for flight test, mission planning, and maintenance workflows.
- [ ] Add richer dashboard widget configuration such as thresholds, units, and selected vehicle/fleet scoping.
- [ ] Add persisted application settings beyond the dashboard layout.
- [ ] Persist completed onboard MAVLink log downloads as raw `.tlog` or `.bin`
  files once `LOG_DATA` byte assembly is promoted from operation progress to
  durable file output.

## Test Ownership

- [ ] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, telemetry integration, and Animus.
- [ ] Keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [ ] Add path-aware verification selection so agent and human checks can map changed files to the smallest defensible command set.
- [ ] Build richer visual verification reports that combine metrics, plots, screenshots, logs, and manifests in one reviewable artifact.

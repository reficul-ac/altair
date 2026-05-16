# Altair Active Backlog

This file tracks near-term repository work. Roadmap and design context live in:

- [Architecture](docs/architecture.md)
- [Vehicle layer](docs/vehicle_layer.md)
- [Simulation and Monte Carlo](docs/simulation_and_mc.md)
- [Testing](docs/testing.md)
- [Animus operator controls](docs/animus_operator_controls.md)

## Baseline Health

- [ ] Keep CI green for formatting, Debug/Release CMake, CTest, and Animus.
- [ ] Keep generated build, CSV, plot, and Animus distribution artifacts out of the worktree.
- [ ] Periodically refresh Animus dependencies on the Node 24 LTS path and require `npm audit --audit-level=moderate` to pass.

## Vehicle And SITL

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Document mass, wing area, speed limits, control limits, safe actuator positions, and aero data provenance.
- [ ] Expand mixer tests for edge cases, saturation, sign conventions, and failsafe/disarmed outputs.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Add deterministic wind, turbulence, sensor noise, bias hooks, and randomized Monte Carlo inputs.
- [ ] Extend trim coverage beyond the initial fixed-wing level-flight case.
- [ ] Add aero database lookup, interpolation, validation, and provenance before replacing scalar aero formulas.

## Telemetry And Replay

- [ ] Define required telemetry packets for SITL, embedded, and log replay.
- [ ] Add packet versioning and compatibility checks.
- [ ] Add log-to-replay tooling for deterministic regression tests.

## Animus

- [x] Fix live capture flight workspace rendering so the 3D scene is nonblank under `python3 tools/python/capture_animus_sitl.py`.
- [x] Resolve flight workspace topbar/status-strip overlap at the default `1440x900` capture viewport.
- [x] Populate map and inspector live workspaces during SITL capture from browser bridge telemetry.
- [x] Add firmware-specific readiness checks beyond generic MAVLink telemetry.
- [x] Make emergency action UX, command cancellation, stale-link protection, and duplicate-GCS handling explicit.
- [x] Add durable command audit basics with command payload, confirmation, ACK/NACK, timeout, cancellation, and failure reason.
- [x] Add MAVLink v2/signing awareness to live diagnostics.
- [ ] Add retry dispatch and retry audit semantics for live commands.
- [ ] Expand durable command audit history with operator and session identity.
- [ ] Add broader dialect-driven MAVLink decoding beyond the currently supported message set.
- [ ] Add typed command workflows for high-consequence live actions.
- [ ] Add richer protocol-backed GCS workflows for parameter editing, mission transfer, map/terrain, log, and camera operations.

## Test Ownership

- [ ] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, telemetry integration, and Animus.
- [ ] Move generic Bayek math/control/sim/trim/host-SITL unit coverage into the Bayek repo when that submodule is ready to accept the tests.
- [ ] Until then, keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [ ] Add path-aware verification selection so agent and human checks can map changed files to the smallest defensible command set.
- [ ] Build richer visual verification reports that combine metrics, plots, screenshots, logs, and manifests in one reviewable artifact.

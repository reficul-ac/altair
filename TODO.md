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

- [ ] Keep CI green for formatting, Debug/Release CMake, CTest, and Qt Animus.
- [ ] Keep generated build, CSV, plot, and Animus capture artifacts out of the worktree.

## Vehicle And SITL

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Document mass, wing area, speed limits, control limits, safe actuator positions, and aero data provenance.
- [x] Expand mixer tests for edge cases, saturation, sign conventions, and failsafe/disarmed outputs.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Wire Bayek wind, turbulence, sensor noise, and bias hooks into Altair SITL
  and Monte Carlo scenarios after the reusable hooks exist.
- [ ] Add randomized Monte Carlo inputs for Altair-owned dispersions, guardrails,
  and summary CSV semantics.
- [ ] Document Altair aero data provenance before replacing scalar aero formulas
  with reusable Bayek aero database helpers.

## Telemetry And Replay

- [ ] Define required telemetry packets for SITL, embedded, and log replay.
- [x] Add Altair replay schema compatibility checks around imported telemetry
  and live viewer session snapshots.
- [ ] Add log-to-replay tooling for deterministic regression tests.

## Animus

- [ ] Complete the remaining Animus Qt migration work: QGC-derived 2D
  provider/cache workers, offline MBTiles/PMTiles serving, Cesium terrain,
  mission/fence/rally overlays, and broader Qt parity.
- [ ] Move Animus Qt UDP telemetry receive/decode onto an explicit worker thread
  if UI rendering or map loading can contend with packet ingestion under load.
- [x] Replace Animus Qt capture readiness artifacts with real map-2d,
  terrain-3d, and setup screenshots plus nonblank scene checks.
- [ ] Add QtLocation-backed Animus Qt map rendering once CI and operator
  installs have a portable runtime package strategy.
- [ ] Port still-desired retired dashboard, inspector, replay import/export,
  flight view, and guarded command workflows into Qt Animus with Qt-native
  tests and capture coverage.
- [x] Extend Animus Qt screenshot analysis beyond nonblank PNG checks to catch
  obvious overlap, clipping, and workspace selection regressions.
- [ ] Add Animus Qt local tile serving for validated XYZ map packs, then MBTiles,
  before adding PMTiles support.
- [x] Retire the obsolete TypeScript Animus app, generated dependency tree,
  interaction harnesses, and old CI lane now that Qt Animus is canonical.

## Test Ownership

- [ ] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, telemetry integration, and Animus.
- [ ] Keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [x] Add path-aware verification selection so agent and human checks can map changed files to the smallest defensible command set.
- [ ] Build richer non-Animus visual verification reports that combine metrics,
  plots, logs, and manifests in one reviewable artifact.

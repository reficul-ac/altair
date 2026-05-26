# Altair Active Backlog

This file tracks near-term repository work. Roadmap and design context live in:

- [Architecture](docs/architecture.md)
- [Vehicle layer](docs/vehicle_layer.md)
- [Simulation and Monte Carlo](docs/simulation_and_mc.md)
- [Testing](docs/testing.md)

Reusable Bayek framework follow-up is tracked separately in
[`bayek/TODO.md`](bayek/TODO.md).

## Baseline Health

- [ ] Keep CI green for formatting, Debug/Release CMake, and CTest.

## Animus

Detailed architecture and phase checklist:
[`docs/animus_architecture.md`](docs/animus_architecture.md#25-actionable-design-checklist).

- [x] Phase A: create the self-contained `animus/` future repo root with
  independent CMake/Conan build files and generated-data ignore policy.
- [x] Phase B: define Animus module ownership, public header layout, core data
  contracts, cache keys, and error-reporting conventions.
- [x] Phase C: implement `geo_core` tile math and Web Mercator tests.
- [x] Phase D: define sample terrain data, local XYZ tile layout, and offline
  tile preparation/validation tools.
- [x] Prepare or download a real Lake Tahoe Phase D tile pack under ignored
  Animus data paths and validate it with `validate_tile_pyramid.py`.
- [x] Phase E: build the native `terrain_lab` OpenGL/GLFW/GLEW render
  foundation without coupling it to Altair, Bayek, telemetry, or the final app.
- [x] Run the Phase E `terrain_lab` native-window manual check on a workstation
  with a real display and confirm the clear color and triangle are visible.
  In the Codex/tmux shell, export `DISPLAY=:0` and
  `XAUTHORITY=/run/user/1000/.mutter-Xwaylandauth.17JVP3`; with those values,
  `terrain_lab --frames 600` opens on the native Intel GPU and exits cleanly.
- [x] Phase F: render local imagery/elevation terrain from one tile through a
  seamless fixed 3x3 same-LOD patch.
- [ ] Phase G: extract reusable terrain systems from `terrain_lab` into
  `terrain_core` while preserving `terrain_lab` as the regression harness.
- [ ] Phase H: add async terrain streaming, tile state debug visualization, GPU
  upload budgets, camera tile wishlist, and no-holes parent fallback.
- [ ] Phase I: add bounded cache hierarchy, LRU eviction, tile synthesis,
  GeoTIFF extraction, and elevation/bathymetry merge.
- [ ] Phase J: create the full native `apps/animus` shell only after
  `terrain_core` is stable.
- [ ] Phase K: add delayed telemetry playback with CSV/JSON first, then
  MCAP/Protobuf/HDF5 after the simple path works.
- [ ] Phase L: add advanced overlays, remote tile providers, MBTiles/SQLite
  cache metadata, cache prewarming, datum correction, and export workflows.

## Vehicle And SITL

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Document mass, wing area, speed limits, control limits, safe actuator positions, and aero data provenance.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Wire Bayek wind, turbulence, sensor noise, and bias hooks into Altair SITL
  and Monte Carlo scenarios after the reusable hooks exist.
- [ ] Add randomized Monte Carlo inputs for Altair-owned dispersions, guardrails,
  and summary CSV semantics.
- [x] Add debugger workflow polish for SITL, such as IDE launch config
  generation or broader noninteractive LLDB smoke coverage.
- [ ] Document Altair aero data provenance before replacing scalar aero formulas
  with reusable Bayek aero database helpers.

## Telemetry And Replay

- [x] Add log-to-replay tooling for deterministic regression tests: canonical
  `.altlog` capture, `.tlog` export, replay/export commands, summary JSON,
  CSV round-trip compatibility, and baseline-vs-candidate diff expectations.
- [x] Version and generate `docs/telemetry_contract.json` plus schema/docs,
  CSV header checks, compatibility checks, and fresh/stale/unsupported
  field-state semantics.

## Workflow, Artifacts, And Verification

- [ ] Add a unified `tools/python/altairctl.py` CLI for doctor, build, run,
  Monte Carlo, replay, capture, verify, clean, map, terrain, and log workflows
  while preserving existing scripts as wrappers.
- [ ] Create a scenario/test-card registry under `cases/` with YAML validation,
  list/run/report commands, smoke and cruise6dof seed cases, docs, and
  integration tests.
- [ ] Define a canonical versioned `.altlog` format with inspect, export,
  replay, and diff commands, CSV round-trip compatibility, `.tlog` export,
  docs, and fixture coverage.
- [ ] Add a simulation credibility scorecard and parameter provenance manifest,
  including `docs/model_credibility/`, a validator, generated summary, and
  report/CLI exposure of credibility tier.
- [ ] Add a Monte Carlo YAML campaign framework under `mc/campaigns/` with
  deterministic seed/run-index replay, richer CSV/JSON/HTML outputs, failure
  artifacts, and local smoke coverage.
- [ ] Define explicit CI/local verification evidence tiers covering PlatformIO
  compile, sanitizer/static-analysis lanes, SITL artifacts, and
  integration with `verify_agent_work.py` or future `altairctl verify`.
- [ ] Standardize HTML artifact reports and predictable ignored artifact roots
  for SITL, Monte Carlo, replay, and agent verification bundles.
- [ ] Add lightweight requirements and verification traceability under
  `docs/requirements/` with a checker, generated matrix/test-card summary, and
  report integration.
- [ ] Add a replay-diff workflow for CSV and future `.altlog`
  baseline-vs-candidate analysis.
- [ ] Add embedded/HAL maturity checks and PlatformIO compile verification.

## Test Ownership

- [x] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, and telemetry integration.
- [x] Keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [ ] Build richer visual verification reports that combine metrics, plots,
  logs, and manifests in one reviewable artifact.

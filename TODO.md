# Altair Active Backlog

This file tracks near-term repository work. Roadmap and design context live in:

- [Architecture](docs/architecture.md)
- [Vehicle layer](docs/vehicle_layer.md)
- [Simulation and Monte Carlo](docs/simulation_and_mc.md)
- [Testing](docs/testing.md)
- [Animus architecture plan](docs/animus_architecture.md)

Reusable Bayek framework follow-up is tracked separately in
[`bayek/TODO.md`](bayek/TODO.md).

## Baseline Health

- [ ] Keep CI green for formatting, Debug/Release CMake, and CTest.
- [x] Reformat `animus/apps/animus/src/layer_offline.cpp` and
  `animus/apps/animus/src/layer_offline.hpp` so `format_repo.py --check` passes
  without touching unrelated files.
- [ ] Reformat `animus/apps/animus/src/mavlink_inspector.cpp`,
  `animus/apps/animus/src/mavlink_inspector.hpp`, and
  `animus/tests/mavlink_inspector_tests.cpp` so `format_repo.py --check` passes.

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
- [x] Implement the documented Animus vehicle asset registry and GLB fallback
  path after the operator UI/entity overlay foundation is stable.
- [x] Add per-entity Animus vehicle assignment policy and UI after the default
  Generic RC Plane model path is exercised in operator workflows.
- [ ] Add selected-entity tail length preference once the overlay renderer has
  app-local tail-length wiring.
- [ ] Add persisted Animus selected-vehicle test metadata for test name, phase,
  and target values once the display-only flight-test card placeholders are
  exercised.
- [ ] Expose MAVLink heartbeat age to the Animus operator status ribbon once
  heartbeat observation is available outside Developer diagnostics.
- [ ] Add Animus review-tape marker sources for low link Hz, terrain
  fallback/synthetic-under-vehicle, speed/climb excursions, geofence, capture
  events, and model fallback when those runtime/session sources are available.
- [ ] Add Animus ghost replay baseline telemetry loading, comparison,
  and ghost track rendering on top of the Phase 11 current-run summary.
- [ ] Add headless Animus workspace-layout transition tests for switching and
  resetting layouts once the ImGui layout application logic is factored into a
  pure model helper.

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

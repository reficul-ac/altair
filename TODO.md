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
  CSV header checks, Animus topic documentation, compatibility checks, and
  fresh/stale/unsupported field-state semantics.

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
  compile, sanitizer/static-analysis lanes, SITL/Animus artifacts, and
  integration with `verify_agent_work.py` or future `altairctl verify`.
- [ ] Standardize HTML artifact reports and predictable ignored artifact roots
  for SITL, Monte Carlo, replay, Animus capture, and agent verification bundles.
- [ ] Add lightweight requirements and verification traceability under
  `docs/requirements/` with a checker, generated matrix/test-card summary, and
  report integration.
- [ ] Add a replay-diff workflow for CSV and future `.altlog`
  baseline-vs-candidate analysis.
- [ ] Extend `.altlog` replay beyond CSV and raw MAVLink export into a
  timeline-driven Animus replay importer with scrubbed field-state playback.
- [ ] Add embedded/HAL maturity checks and PlatformIO compile verification.

## Animus

- [ ] Complete the remaining Animus Qt migration work: offline PMTiles serving,
  offline map/terrain pack manager, manifest/checksum verification, setup
  status, import/remove/list commands, seeded-cache compatibility,
  mission/fence/rally overlays, and broader Qt parity.
- [ ] Add a real Altair-local QtLocation `QGroundControl` provider plugin after
  Qt 6 Location is available in the target environment.
- [ ] Add multi-vehicle support: route telemetry by MAVLink system/component
  ID, per-system vehicle models, active vehicle selection, separate
  history/trails/status, diagnostics identity, tests, documented limitations,
  and graceful degradation toward the documented 12-vehicle analysis target.
- [ ] Build Qt-native Flight, Dashboard, Inspector, Replay, Plan, Setup
  readiness, and guarded-command workflows only for features still wanted from
  the retired UI, with capture/test coverage for each workspace.
- [ ] Extend the first-pass Animus telemetry/link diagnostics beyond packet
  counters, decode errors, link freshness, MAVLink system/component identity,
  armed state, GPS, mission, home, and terrain summaries: add firmware/mode
  labels, readiness panel fields, battery/readiness summaries, packet age by
  message family, update rates, jitter, dropped/decoded counts,
  clock/source timestamps where available, sim metadata, model credibility tier,
  per-field fresh/stale/unsupported/unknown states, Qt tests, screenshots, and
  docs.
- [ ] Move UDP receive/decode, map tile IO, and terrain/cache work
  behind explicit worker boundaries so rendering, synchronous tile reads,
  and map loading cannot contend with telemetry ingestion.
- [ ] Feed Animus capture bundles into the standard artifact report path with
  manifest metadata, screenshot links, scene diagnostics, and ignored output
  roots shared with SITL/replay/agent verification reports.
- [ ] Add QtLocation-backed Animus Qt map rendering once CI and operator
  installs have a portable runtime package strategy; Ubuntu 24.04 repositories
  available on this host do not provide Qt 6 Location.
- [ ] Add an energy-state and fixed-wing flight-envelope monitor for Flight,
  2D, and 3D views: airspeed, groundspeed, altitude AGL/MSL, vertical speed,
  attitude/load limits, stall/overspeed margins, climb/sink trend, and explicit
  unknown states when required telemetry is absent.
- [ ] Add navigation-quality and estimator-health panels for Setup and Flight
  views: GPS fix type, satellite count, HDOP/VDOP or covariance where
  available, EKF/estimator flags, position/velocity innovations, compass/IMU
  health, and degraded navigation warnings.
- [ ] Add route-progress and mission-execution awareness: active waypoint,
  cross-track error, distance/time to next waypoint, distance/time to home,
  mission completion estimate, loiter/hold state, and clear no-mission or
  unsupported-mission states.
- [ ] Add terrain and clearance awareness beyond the current terrain
  placeholder: AGL estimate, terrain report age, home-relative altitude,
  projected clearance along recent or active path, terrain-data availability,
  and caution states when terrain and altitude references disagree.
- [ ] Add mission/fence/rally analysis overlays with mission path, active leg,
  terrain-clearance corridor, actuator margin, wind/uncertainty/event overlays,
  persisted toggle state, event explanation panel, tests, and docs.
- [ ] Add wind and environment awareness for GNC debugging: estimated wind
  vector, headwind/crosswind components relative to track or mission leg,
  airspeed-vs-groundspeed consistency, and SITL weather/noise provenance during
  simulated sessions.
- [ ] Add control-authority and actuator-margin diagnostics: normalized
  roll/pitch/yaw/throttle demand, servo or actuator outputs, saturation flags,
  trim bias, RC override/manual input visibility, and failsafe/disarmed output
  state.
- [ ] Add an operator event timeline shared by live and replay views: mode
  changes, arming state, failsafe transitions, GPS/estimator degradation,
  command attempts, mission item changes, telemetry dropouts, and user-added
  markers.
- [ ] Add field-flight readiness profiles for Setup: preflight, launch,
  mission, recovery, and postflight check groups that summarize link, GPS,
  estimator, battery, mission, terrain, parameters, and log-recording readiness
  without enabling live writes by default.
- [ ] Audit available Altair/Bayek/MAVLink telemetry for fields that should be
  first-class 2D/3D overlays versus fields better exposed through custom
  operator widgets. Include passive fields from `SYS_STATUS`, `BATTERY_STATUS`,
  `POWER_STATUS`, `VFR_HUD`, `WIND_COV`, `NAV_CONTROLLER_OUTPUT`,
  `LOCAL_POSITION_NED`, `SERVO_OUTPUT_RAW`, `RC_CHANNELS`, `ESTIMATOR_STATUS`,
  `EKF_STATUS_REPORT`, and `STATUSTEXT`, and keep GNC-derived calculations
  centralized in Qt C++ model/helper code.
- [ ] Add operator-built custom telemetry widgets backed by JSON/QML-safe
  configs and telemetry-contract binding, including numeric, status, bar, and
  sparkline widgets, fresh/stale/unsupported states, load errors, and Qt tests.
- [ ] Automatically fetch and display active mission plan/waypoint data needed
  by route-progress views when Altair/Bayek or the connected vehicle exposes
  mission data.
- [ ] Add a read-only parameter view for connected vehicles first: source,
  value, unit, range, validation state, runtime-updatable status, and no
  implied persistent writes until write policy is explicit.
- [ ] Automatically record received telemetry to a `.tlog` during each live
  SITL/Animus session; persist the operator-selected save path, expose a CLI
  flag, integrate artifact manifest/report output, and warn/fail clearly when
  recording is unavailable. Bridge-side `.altlog` recording and raw `.tlog`
  export exist; Qt operator path persistence/report integration remain. TODO: revisit the default save location before
  hardware use.
- [x] Add Animus dark/light mode switching with consistent theme behavior,
  readable diagnostics, non-color-only safety state, persisted operator
  preference, and screenshot/manual verification.
- [ ] Replace the Terrain 3D placeholder with a true trajectory analysis
  workspace: synchronized 3D/local trajectory views, plots, scrubber,
  baseline/candidate comparison, exports, `.altlog`/CSV input, tests, capture,
  and docs.
- [ ] Add local aircraft model import for Terrain 3D after the operator file
  policy, supported formats, and offline asset packaging are defined.
- [ ] Port still-desired retired dashboard, inspector, replay import/export,
  flight view, and guarded command workflows into Qt Animus with Qt-native
  tests and capture coverage.
- [ ] Diagnose Terrain 3D trackpad event delivery in Qt WebEngine and add a
  non-laggy Space+trackpad rotate path without a full-scene QML input overlay.
- [ ] Implement the behavior-preserving Animus UI declutter pass from
  `docs/animus_bloat_audit.md`: shared QML status/overlay/control components,
  progressive diagnostics disclosure, less always-visible debug chrome, and
  before/after screenshot comparison.

## Test Ownership

- [x] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, telemetry integration, and Animus.
- [x] Keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [ ] Build richer non-Animus visual verification reports that combine metrics,
  plots, logs, and manifests in one reviewable artifact.

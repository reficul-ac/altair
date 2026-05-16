# Altair Todo

This list tracks the work needed to move Altair from a building repository with deterministic skeletons to a fully running vehicle simulation stack with useful SITL and Monte Carlo workflows.

## 1. Baseline Health

- [x] Turn on compiler warnings for host CMake targets.
- [x] Decide which warnings should be fatal in CI.
- [x] Add a Debug and Release build matrix to CI.
- [x] Add a CI check that verifies the Bayek submodule is initialized.
- [x] Add a documented local developer command set for configure, build, test, SITL, and Monte Carlo.
- [x] Add generated build, CSV, and plot artifacts to `.gitignore` if missing.
- [x] Add a lightweight formatting rule or script for C, C++, CMake, and Python files.

Acceptance check:

- [ ] A clean checkout can run the documented commands and produce passing tests plus SITL and Monte Carlo output.

## 2. Vehicle Model Definition

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Define control surface conventions, sign conventions, and actuator units.
- [ ] Define motor/throttle behavior and saturation rules.
- [x] Move fixed-wing sim parameter construction into an Altair-owned helper and validate consistency with `altair_default_params()`.
- [x] Add parameter validation tests for physical ranges and internal consistency.
- [ ] Document mass, wing area, speed limits, control limits, and safe actuator positions.
- [ ] Document aero data provenance and intended fidelity level, including whether values come from estimates, analysis tools, wind tunnel data, or flight-test identification.
- [ ] Decide whether parameters remain compile-time constants or move toward loadable config.

Acceptance check:

- [ ] Vehicle params are traceable to docs, tested for bounds, and used consistently by mixer, FSW, and sim code.

## 3. Mixer And Actuator Layer

- [ ] Expand mixer tests for edge cases, null inputs, saturation, and sign conventions.
- [ ] Add actuator normalization helpers if host sim and embedded output need different units.
- [ ] Add explicit safe-output behavior for disarmed and failsafe modes.
- [ ] Document actuator output ranges for host, simulation, and Arduino HAL.
- [ ] Add replay tests proving mixer outputs are deterministic.

Acceptance check:

- [ ] Mixer behavior is fully specified by tests and docs, including all limit and failsafe cases.

## 4. Flight Software Core

- [x] Make FSW input validation explicit.
- [x] Add tests for mode selection boundaries, including invalid `dt_s`, missing GPS, disarm, manual, stabilize, and failsafe.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Add reset and initialization tests around repeated runs.
- [ ] Separate control-law tuning constants from hard-coded Bayek internals.
- [ ] Add clear behavior for sensor dropout and stale timestamps.
- [ ] Add replay fixtures that cover manual, stabilize, and failsafe transitions.

Acceptance check:

- [ ] `altair_fsw_step()` has deterministic, tested behavior for nominal flight, bad inputs, reset, and mode transitions.

## 5. Vehicle Simulation

- [x] Replace the toy plant with a more useful fixed-wing point-mass or 6-DOF-lite model.
- [x] Define the simulation state vector and units.
- [ ] Add wind, turbulence, sensor noise, and bias hooks.
- [ ] Add deterministic seeding for all stochastic sim effects.
- [x] Add bounds checks for state, actuator, and sensor values.
- [x] Add scenario definitions for takeoff-like acceleration, cruise, turns, descent, and failsafe.
- [x] Add mission and condition-file driven SITL scenarios for repeatable vehicle-level cases.
- [x] Add profile-level deterministic replay and plausibility gates for `cruise`, `takeoff`, `turn`, `descent`, and `failsafe` before adding wind/noise.
- [x] Add tests for deterministic simulation replay.
- [x] Add reusable Bayek trim solving support and fixed-wing level-flight trim mechanics.
- [x] Move reusable fixed-wing level-flight trim mechanics into Bayek while keeping Altair responsible for enablement policy, limits, and concrete parameters.
- [ ] Extend trim coverage beyond the initial fixed-wing level-flight case, including off-axis initial states and future multirotor support.
- [ ] Add trim tuning docs that explain residual definitions, bounds, tolerances, and failure modes.
- [ ] Add an aero database/evaluator that looks up coefficients by flight condition instead of relying only on scalar formulas.
- [ ] Use OpenVSP + VSPAERO as the first-choice workflow for generating aircraft-level aero database points from a 3D vehicle model.
- [ ] Sweep `alpha`, `beta`, control deflections, representative airspeeds/dynamic pressure, and propulsion settings where applicable when generating aero database data.
- [ ] Use AVL as a fast independent cross-check for stability and control derivatives.
- [ ] Use XFOIL/XFLR5 for airfoil, low-Reynolds-number, and section-polar data that feed or sanity-check the aircraft-level model.
- [ ] Treat SU2/OpenFOAM as later higher-fidelity CFD spot-check tools rather than the first aero database implementation path.
- [ ] Version generated aero data with tool versions, geometry revision, sweep settings, assumptions, and known invalid regions.
- [ ] Calibrate and correct the aero database against flight-test or wind-tunnel data when measured data becomes available.
- [ ] Define aero database axes and outputs, starting with `alpha`, `beta`, and airspeed/dynamic pressure for `CL`, `CD`, `CY`, `Cl`, `Cm`, and `Cn`.
- [ ] Add deterministic interpolation and out-of-range clamping/validation tests for aero table lookup.
- [ ] Replace hard-coded fixed-wing lift/drag/moment formulas with a coefficient-to-force/moment pipeline while preserving current behavior through equivalent starter tables.
- [ ] Add actuator/control-surface effectiveness schedules based on flight condition, including elevator, aileron, rudder, and throttle/propulsion effects.
- [ ] Add richer actuator modeling: surface position state, lag/rate limits, saturation, deadband, and future load/hinge effectiveness limits.
- [ ] Keep reusable interpolation/model-evaluation machinery in Bayek and Altair-specific aero data, geometry, sign conventions, and parameter provenance in Altair.
- [x] Document model limitations clearly so results are not overinterpreted.

Acceptance check:

- [ ] A deterministic scenario can run end-to-end and produce plausible state, sensor, FSW, and actuator histories.

## 6. SITL Runner

- [x] Add command-line options for duration, step size, scenario, seed, and output path.
- [x] Write SITL output to CSV instead of only stdout.
- [x] Include inputs, estimates, modes, actuators, and plant truth in SITL logs.
- [x] Add nonzero exit codes for invalid scenario config, unbounded output, or failed assertions.
- [x] Add a short smoke scenario for CI.
- [x] Add a longer local scenario for developer inspection.
- [x] Add Python or shell wrapper to run SITL and summarize key metrics.
- [x] Add optional real-time wall-clock pacing.
- [x] Add 3D browser playback from SITL CSV logs.
- [x] Add case files and condition files for repeatable SITL configuration changes.
- [x] Add trim trigger/status support through SITL conditions and cruise6dof CSV logs.
- [x] Add documentation with example commands and expected outputs.

Acceptance check:

- [x] `sitl_runner` can be invoked reproducibly from the command line and produces an inspectable flight log.

## 7. Monte Carlo Runner

- [x] Add command-line options for seed, runs, duration, step size, scenario family, and output path.
- [x] Log enough per-run metadata to reproduce failures.
- [ ] Add randomized initial conditions, wind, sensor noise, parameter perturbations, and command profiles.
- [x] Define pass/fail metrics for bounded actuators, no NaNs, safe modes, speed, altitude, and attitude limits.
- [x] Emit summary CSV with pass/fail status and failure reason.
- [ ] Add optional per-run detailed logs for failed cases.
- [x] Add CI-scale Monte Carlo smoke run.
- [ ] Add local-scale Monte Carlo command for broader sweeps.
- [ ] Add summary tooling for mean, min, max, percentile, and failure counts.

Acceptance check:

- [ ] Monte Carlo runs are reproducible by seed and produce actionable failure summaries.

## 8. Telemetry And Replay

- [ ] Decide what telemetry packets are required for SITL, embedded, and log replay.
- [ ] Add packet versioning and compatibility checks.
- [ ] Add telemetry coverage for mode, state estimate, actuator outputs, and health/failsafe status.
- [ ] Add log-to-replay tooling for deterministic regression tests.
- [x] Store small replay fixtures under tests without committing large generated logs.
- [x] Add replay comparison tolerances and clear diff output.

Acceptance check:

- [ ] A SITL or Monte Carlo run can produce data that is replayable in a regression test.

## 9. Animus / Ground Station Parity

Animus currently provides a useful MAVLink/SITL debugger and viewer shell. Treat PX4 Hawkeye and QGroundControl parity as roadmap targets, not as completed replacement parity.

Current status split:

- Implemented: MAVLink v1 UDP ingest, QGroundControl forwarding, heartbeat/attitude/GPS/local/global position/VFR HUD/status/basic mission message decoding, 3D live view, map trail, replay import for Altair replay JSON and simple CSV/TSV logs, a basic MAVLink inspector with numeric field plotting and CSV export, local waypoint plan editing, guarded SITL-only command stubs, and simple waypoint mission upload packet emission.
- Shell or placeholder only: Plan survey/corridor/structure/landing tools, geofence/rally editing, setup/calibration editing, parameter edit/upload workflows, camera capture/record/subtitle controls, full GCS command forms, full mission transfer state synchronization, and QGC-style Analyze tools.
- Unsupported or out of scope today: raw MAVLink console writes, live-vehicle write authority, MAVLink signing/key management, MAVLink v2/signing parity, firmware setup/calibration changes, and onboard log management.
- Roadmap parity: PX4 Hawkeye/QGroundControl-style planning, setup, video, logs, parameters, safety, persistence, firmware compatibility, release readiness, and live-vehicle acceptance.

Audit and scope:

- [x] Re-audit Animus parity claims and mark placeholder/shell features separately from implemented features.
- [x] Decide which full-GCS features should stay out of scope for the debugger-oriented Altair/SITL workflow, and show explicit unsupported-feature states where needed.

Operational safety:

- [x] Define command authority states for read-only, SITL-writable, trusted live-link writable, and maintenance/setup modes, with visible UI state and clear downgrade behavior.
- [ ] Add arming and preflight gates for GPS/estimator health, link freshness, failsafe state, battery/power, mission validity, operator confirmation, and firmware-specific readiness checks.
- [ ] Design emergency action UX for disarm, kill/emergency stop, hold/pause, return-to-launch, land, and command cancellation, including accidental-click protection and post-action feedback.
- [ ] Add a durable command audit log covering operator identity/session, vehicle, link, command payload, confirmation, ACK/NACK/result, retries, timeout, and failure reason.
- [ ] Prevent unsafe writes on stale links, reconnect races, conflicting vehicle IDs, unsupported modes, or duplicate-GCS conflicts; surface blocked actions as explicit states rather than silent failures.
- [ ] Define field-operation acceptance criteria for live-vehicle use, including preflight checklist completion, emergency action latency, stale-link protection, and safe failure behavior.

Firmware compatibility:

- [ ] Maintain a compatibility matrix for PX4, ArduPilot, and Altair covering supported vehicle types, firmware versions, MAVLink dialects, MAVLink v1/v2 coverage, and signing expectations.
- [ ] Map firmware-specific flight modes, custom modes, arming states, failsafe states, mission states, and unsupported or unknown states into typed Animus UI models. Initial PX4/ArduPilot heartbeat mode, arming, readiness, and unsupported/unknown UI states are implemented; failsafe and full mission-state mapping remain.
- [ ] Add capability discovery from heartbeat, AUTOPILOT_VERSION, protocol version, parameters, mission support, camera support, log support, and component metadata where available.
- [ ] Support parameter metadata sources and firmware-specific constraints for PX4, ArduPilot, and Altair, including units, ranges, reboot requirements, volatile parameters, and unknown metadata fallback.
- [ ] Keep unsupported firmware workflows visible but disabled until protocol support, firmware mapping, and acceptance tests exist.

MAVLink protocol and links:

- [ ] Add MAVLink v2 support, packet signing awareness, dialect/schema-driven decoding, and broader message coverage.
- [ ] Add robust link management: multiple UDP/TCP/serial links, per-link health, reconnect behavior, endpoint selection, and routing rules.
- [ ] Add guarded GCS command workflows with typed action forms, ACK tracking, retries/timeouts, result history, and vehicle-specific capability gating.
- [ ] Add MAVLink parameter protocol support: fetch, cache, search, edit, validate, upload, save/load parameter files, diff against defaults, and rollback failed edits.
- [ ] Add complete mission protocol support: download, upload with request/ACK sequencing, clear, partial failure handling, resume/retry, and vehicle mission-state synchronization.

Data persistence and project model:

- [ ] Define durable models for `VehicleProfile`, `LinkProfile`, `MissionPlan`, `ParameterSet`, `LogSource`, `OperatorSettings`, and `FirmwareCapabilities`.
- [ ] Persist saved vehicles, connection profiles, missions, geofences, rally points, parameter snapshots, imported/downloaded logs, operator settings, map/cache settings, and recent project state.
- [ ] Add import/export, backup/restore, schema migrations, corruption recovery, and versioned project files suitable for sharing field setups between machines.
- [ ] Preserve metadata for vehicle identity, firmware identity, link source, timestamps, coordinate frame, home/planned-home, parameter source, and mission upload/download history.

Mission planning:

- [ ] Expand mission planning beyond simple waypoints: map editing, item reorder/delete, altitude modes, fixed-wing takeoff/landing, survey, corridor scan, geofence, rally points, terrain hooks, mission statistics, and local save/restore.

Maps and terrain:

- [ ] Add online and offline map provider support with configurable tile sources, attribution/licensing metadata, local tile cache limits, cache eviction, and no-network behavior.
- [ ] Add elevation and terrain infrastructure for terrain-following planning, terrain profiles, altitude validation, home/planned-home handling, and mission statistics.
- [ ] Add geocoder/search, coordinate format conversion, datum/frame handling, local/NED/global coordinate transforms, and explicit no-map fallback workflows.
- [ ] Add map data configuration and license documentation so packaged builds do not assume unavailable or unlicensed third-party services.

Live view, replay, and logs:

- [ ] Improve 3D visualization for live and replay: timeline-coupled playback, multiple vehicle ghost trails, camera presets, event overlays, attitude/trajectory diagnostics, and high-rate telemetry performance limits.
- [ ] Add native log ingestion for ULog and MAVLink telemetry logs, preserving metadata, timestamps, topics/messages, parameters, events, and vehicle identity.
- [ ] Add onboard log listing, download, cancel/resume, delete where safe, MAVLink `.tlog` recording/replay, PX4/ArduPilot log import, metadata retention, and large-log performance limits.
- [ ] Add offline/mock workflows with realistic synthetic MAVLink streams, parameter sets, missions, logs, and regression fixtures.

Analysis and troubleshooting:

- [ ] Build an analysis workspace for arbitrary MAVLink/log signals: choose vehicles/messages/fields, overlay plots, derive signals, synchronize cursors with 3D/map replay, export CSV/PNG, and save plot presets.
- [ ] Add QGC-style Analyze tools: MAVLink message browser, console/status text history, log download/import/export, link diagnostics, and troubleshooting capture bundles.

Setup, calibration, and media:

- [ ] Add setup/configuration surfaces only where safe for Altair: sensors/status, flight modes, safety, tuning, camera, joystick, app settings, and explicit unsupported-feature states.
- [ ] Add real setup and calibration workflows for sensors, radio, flight modes, power, motors/actuators, safety, airframe, tuning, firmware/vehicle identity, and calibration result persistence.
- [ ] Require protocol-backed setup transactions, firmware-specific validation, reversible edits where possible, and explicit unsupported states before enabling vehicle-affecting setup controls.
- [ ] Add video/camera support only after real stream plumbing exists: RTP/RTSP/UVC display, MAVLink camera protocol metadata, capture/record controls, and telemetry subtitle export.

Security and key management:

- [ ] Add MAVLink signing key management with OS-backed secret storage where available, key import/export policy, key rotation, and clear unsigned-link warnings.
- [ ] Define the link trust model for UDP/TCP/serial, local SITL, field radios, and replay files, including safe defaults for writable links and duplicate-GCS detection.
- [ ] Redact secrets and sensitive link details from logs, crash reports, screenshots, exports, and troubleshooting bundles.
- [ ] Document threat assumptions for field operation, lab/SITL use, shared project files, and untrusted log/replay imports.

Release and platform readiness:

- [ ] Produce packaged Linux, macOS, and Windows builds with reproducible release artifacts, installer/update path, settings migration, and platform-specific smoke tests.
- [ ] Add code signing/notarization where applicable, crash reporting policy, dependency/license audit, bundled asset license review, and documented release gates.
- [ ] Verify serial, UDP, TCP, file import/export, map cache, secret storage, and GPU/3D behavior on target desktop platforms.

Operator documentation and training:

- [ ] Add first-run setup, field checklist, emergency procedures, troubleshooting, supported/unsupported vehicle matrix, and QGroundControl migration guide.
- [ ] Document operational limitations for each firmware family and keep UI unsupported states aligned with the published support matrix.

Future interfaces:

- [ ] Define a protocol boundary separating raw MAVLink transport from typed command, parameter, mission, geofence, rally, log, camera, setup, and calibration transactions.
- [ ] Define a replay/log model capable of representing MAVLink streams, ULog topics, parameters, events, and derived signals.
- [ ] Define an analysis UI model for selected signals, chart presets, synchronized cursors, and exports.

Acceptance tests:

- [ ] Add acceptance tests covering protocol encoders/decoders, parameter transactions, mission transfers, replay/log import, plotting, and guarded command failure modes.
- [ ] Add protocol transaction tests for lossy links, retries, ACK/NACK handling, timeout recovery, reconnects, duplicate packet handling, and duplicate-GCS conflict handling.
- [ ] Add hardware/SITL acceptance scenarios for arm/disarm, mode change, mission upload/download, parameter edit/rollback, log download, failsafe display, and emergency stop behavior.
- [ ] Add packaged-app smoke tests on target desktop platforms and regression fixtures for PX4, ArduPilot, and Altair MAVLink streams.

Acceptance check:

- [x] The TODO list clearly separates implemented viewer/debug shell behavior from planned PX4 Hawkeye and QGroundControl parity.
- [ ] The roadmap covers QGC controls, parameter upload/change, mission planning, 3D live/replay visualization, telemetry playback, and signal plotting without describing planned parity work as if it already exists.
- [ ] The roadmap covers operational safety, firmware compatibility, setup/calibration, maps/terrain, persistence, release readiness, security, log parity, operator documentation, and live-vehicle validation.

## 10. Embedded And HAL

- [ ] Add PlatformIO compile verification to CI if dependency cost is acceptable.
- [ ] Replace Arduino HAL stubs with a simulated or documented board-facing contract.
- [ ] Define sensor input timing and actuator output timing on embedded targets.
- [ ] Add compile-time checks for memory-sensitive structs and packet sizes.
- [ ] Keep embedded transport and hardware code outside portable Bayek FSW.
- [ ] Add a documented path for hardware-in-the-loop later.

Acceptance check:

- [ ] The Arduino skeleton compiles and its HAL contract matches the host-side FSW input/output model.

## 11. Documentation And Operator Workflow

- [x] Update `README.md` with quickstart commands for build, tests, SITL, and Monte Carlo.
- [x] Add docs for simulation assumptions, scenario files, output CSV schemas, and metrics.
- [ ] Add a development workflow document for adding a new scenario or regression fixture.
- [ ] Add a troubleshooting section for submodules, CMake, CI, and generated artifacts.
- [x] Keep Bayek/Altair ownership boundaries documented as code evolves.
- [ ] Document the SITL trim condition-file workflow with a worked example and guidance for filtering pre-trim rows.

Acceptance check:

- [ ] A new contributor can clone the repo, run tests, run SITL, run Monte Carlo, and understand the output from docs alone.

## 12. Long-Term Domainization

Altair and Bayek should move gradually toward clearer domain ownership as the code grows. Do this opportunistically when a domain becomes large enough to justify a boundary; avoid churn that only renames files without improving ownership, tests, or interfaces.

Bayek long-term domains:

- [ ] Keep `fsw` as the public flight-core facade while domainizing internals behind it.
- [ ] Keep `nav` focused on sensor preprocessing, state estimation, estimator reset, and estimator health.
- [ ] Keep `guidance` focused on command-to-setpoint behavior, independent of where commands originate.
- [ ] Keep `control` focused on controller state and normalized control requests.
- [ ] Split `mode` out of `fault` when mode arbitration grows beyond simple disarm/manual/stabilize/failsafe selection.
- [ ] Keep `fault` focused on sensor health, stale data, actuator health inputs, fault latching, degradation, and recovery rules.
- [ ] Keep `sim` split conceptually between dynamics, sensor models, scenario/runtime execution, deterministic seeds, and replay support.
- [x] Keep generic simulation trim solving in Bayek `sim` rather than Altair-specific runner code.
- [ ] Keep telemetry and replay independent from the FSW step so logs, packets, and transports can evolve separately.

Altair long-term domains:

- [ ] Grow the current mixer layer into an explicit actuation domain when it needs trims, reversals, actuator health masking, slew limits, PWM mapping, or safe-output policy.
- [ ] Keep vehicle model data separate from generic Bayek simulation: mass properties, aero coefficients, actuator geometry, sign conventions, and documented parameter sources belong to Altair.
- [ ] Keep scenarios, Monte Carlo profiles, pass/fail metrics, CSV schemas, and visualization workflow outside portable Bayek FSW.
- [x] Keep reusable fixed-wing trim mechanics in Bayek while Altair owns concrete trim enablement policy, actuator bounds, parameters, and failure handling.
- [ ] Keep board/HAL code responsible for platform timing, sensors, actuator drivers, storage, and transports.
- [ ] Move toward a clearer future shape only as needed:

```text
bayek/
  common/
  fsw/
    nav/
    guidance/
    control/
    mode/
    fault/
  sim/
    dynamics/
    sensors/
    runtime/
  telemetry/
  replay/

altair/
  vehicle_model/
  actuation/
  params/
  scenarios/
  boards/
  tools/
```

Acceptance check:

- [ ] New features have an obvious home, Bayek stays vehicle-agnostic, Altair owns aircraft-specific policy, and domain boundaries become clearer over time without breaking the stable public FSW API unnecessarily.

## Near-Term Milestone

- [x] Add warnings and CI matrix.
- [x] Expand FSW mode and mixer tests.
- [x] Add CLI options and CSV output path support to `sitl_runner`.
- [x] Add CLI options and pass/fail summary fields to `mc_runner`.
- [x] Add README quickstart commands for SITL and Monte Carlo.
- [x] Add one deterministic scenario that is suitable for CI.

Definition of done:

- [x] `cmake -S . -B build`
- [x] `cmake --build build --parallel`
- [x] `ctest --test-dir build --output-on-failure`
- [x] `./build/vehicle/sitl_runner --scenario smoke --output sitl_smoke.csv`
- [x] `./build/vehicle/mc_runner --seed 1 --runs 100 --scenario smoke --output mc_summary.csv`

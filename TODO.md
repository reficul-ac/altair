# Altair Todo

This list tracks the work needed to move Altair from a building repository with deterministic skeletons to a fully running vehicle simulation stack with useful SITL and Monte Carlo workflows.

## 1. Baseline Health

- [x] Turn on compiler warnings for host CMake targets.
- [ ] Decide which warnings should be fatal in CI.
- [x] Add a Debug and Release build matrix to CI.
- [x] Add a CI check that verifies the Bayek submodule is initialized.
- [x] Add a documented local developer command set for configure, build, test, SITL, and Monte Carlo.
- [x] Add generated build, CSV, and plot artifacts to `.gitignore` if missing.
- [ ] Add a lightweight formatting rule or script for C, C++, CMake, and Python files.

Acceptance check:

- [ ] A clean checkout can run the documented commands and produce passing tests plus SITL and Monte Carlo output.

## 2. Vehicle Model Definition

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Define control surface conventions, sign conventions, and actuator units.
- [ ] Define motor/throttle behavior and saturation rules.
- [ ] Add parameter validation tests for physical ranges and internal consistency.
- [ ] Document mass, wing area, speed limits, control limits, and safe actuator positions.
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
- [ ] Add tests for mode selection boundaries, including invalid `dt_s`, missing GPS, disarm, manual, stabilize, and failsafe.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Add reset and initialization tests around repeated runs.
- [ ] Separate control-law tuning constants from hard-coded Bayek internals.
- [ ] Add clear behavior for sensor dropout and stale timestamps.
- [ ] Add replay fixtures that cover manual, stabilize, and failsafe transitions.

Acceptance check:

- [ ] `bayek_fsw_step()` has deterministic, tested behavior for nominal flight, bad inputs, reset, and mode transitions.

## 5. Vehicle Simulation

- [x] Replace the toy plant with a more useful fixed-wing point-mass or 6-DOF-lite model.
- [x] Define the simulation state vector and units.
- [ ] Add wind, turbulence, sensor noise, and bias hooks.
- [ ] Add deterministic seeding for all stochastic sim effects.
- [x] Add bounds checks for state, actuator, and sensor values.
- [ ] Add scenario definitions for takeoff-like acceleration, cruise, turns, descent, and failsafe.
- [ ] Add tests for deterministic simulation replay.
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
- [x] Add documentation with example commands and expected outputs.

Acceptance check:

- [x] `sitl_runner` can be invoked reproducibly from the command line and produces an inspectable flight log.

## 7. Monte Carlo Runner

- [x] Add command-line options for seed, runs, duration, step size, scenario family, and output path.
- [x] Log enough per-run metadata to reproduce failures.
- [ ] Add randomized initial conditions, wind, sensor noise, parameter perturbations, and command profiles.
- [ ] Define pass/fail metrics for bounded actuators, no NaNs, safe modes, speed, altitude, and attitude limits.
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
- [ ] Store small replay fixtures under tests without committing large generated logs.
- [ ] Add replay comparison tolerances and clear diff output.

Acceptance check:

- [ ] A SITL or Monte Carlo run can produce data that is replayable in a regression test.

## 9. Embedded And HAL

- [ ] Add PlatformIO compile verification to CI if dependency cost is acceptable.
- [ ] Replace Arduino HAL stubs with a simulated or documented board-facing contract.
- [ ] Define sensor input timing and actuator output timing on embedded targets.
- [ ] Add compile-time checks for memory-sensitive structs and packet sizes.
- [ ] Keep embedded transport and hardware code outside portable Bayek FSW.
- [ ] Add a documented path for hardware-in-the-loop later.

Acceptance check:

- [ ] The Arduino skeleton compiles and its HAL contract matches the host-side FSW input/output model.

## 10. Documentation And Operator Workflow

- [x] Update `README.md` with quickstart commands for build, tests, SITL, and Monte Carlo.
- [ ] Add docs for simulation assumptions, scenario files, output CSV schemas, and metrics.
- [ ] Add a development workflow document for adding a new scenario or regression fixture.
- [ ] Add a troubleshooting section for submodules, CMake, CI, and generated artifacts.
- [ ] Keep Bayek/Altair ownership boundaries documented as code evolves.

Acceptance check:

- [ ] A new contributor can clone the repo, run tests, run SITL, run Monte Carlo, and understand the output from docs alone.

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

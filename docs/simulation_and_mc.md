# Altair Simulation And Monte Carlo

Altair owns concrete host runners that bind Bayek to `altair_vehicle_interface()`. Bayek owns the generic toy plant documented in [Bayek Simulation](../bayek/docs/simulation.md).

## SITL Plant

Altair currently uses Bayek's deterministic toy plant. It models:

- actuator lag
- actuator saturation through FSW and mixer outputs
- rough roll, pitch, and yaw-rate response
- simple airspeed and altitude evolution
- simulated IMU, GPS, baro, and airspeed samples

This plant is not an Altair flight dynamics model. It exists to validate:

- build and link boundaries
- deterministic closed-loop stepping
- no NaN or Inf outputs
- bounded actuator commands
- repeatable CSV logging

## Altair SITL Runner

`vehicle/sitl_runner.c` runs a fixed number of fixed-size steps. It initializes Bayek with `altair_vehicle_interface()`, prints CSV rows to stdout, and prints a timing summary to stderr.

CSV is used because it is transparent, dependency-free, easy to diff, and easy to consume from Python, spreadsheets, or CI artifacts.

## Altair Monte Carlo Runner

`vehicle/mc_runner.c` is compiled C. It uses a small deterministic linear congruential generator to create seed-based dispersions and binds Bayek through `altair_vehicle_interface()`.

The runner currently varies throttle bias and writes summary metrics:

- run index
- seed
- throttle bias
- final airspeed
- final altitude
- max absolute roll

The current dispersions are placeholders. The key property is replay: the same seed and run count should produce the same CSV.

## Python Tools

Python scripts in `tools/python` are orchestration helpers only:

- `run_mc.py` invokes the compiled runner and writes CSV.
- `plot_mc.py` prints simple summary statistics.

Python is intentionally not used for core simulation logic. That keeps simulation behavior close to what C tests and embedded builds exercise.

## Extension Points

Future Altair simulation work can add:

- richer aircraft dynamics
- wind and turbulence models
- sensor noise and bias models
- actuator failure injection
- scenario files
- binary log output
- HITL transport adapters

Those changes should keep `altair_vehicle_interface()` and `bayek_fsw_step()` as the shared execution boundary.

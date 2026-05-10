# Simulation And Monte Carlo

Host simulation and Monte Carlo are deliberately outside the flight core.

## SITL Plant

`framework/sim/sim_plant.c` implements a deterministic toy plant. It models:

- actuator lag
- actuator saturation through FSW and mixer outputs
- rough roll, pitch, and yaw-rate response
- simple airspeed and altitude evolution
- simulated IMU, GPS, baro, and airspeed samples

This plant is not a flight dynamics model. It exists to validate:

- build and link boundaries
- deterministic closed-loop stepping
- no NaN or Inf outputs
- bounded actuator commands
- repeatable CSV logging

## SITL Runner

`sitl_runner` runs a fixed number of fixed-size steps. It prints CSV rows to stdout and a timing summary to stderr.

CSV is used because it is transparent, dependency-free, easy to diff, and easy to consume from Python, spreadsheets, or CI artifacts.

## Monte Carlo Runner

`framework/mc/mc_runner.c` is compiled C. It uses a small deterministic linear congruential generator to create seed-based dispersions.

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

Python is intentionally not used for core simulation logic. That keeps simulation behavior closer to what C tests and embedded builds exercise.

## Extension Points

Future simulation work can add:

- richer aircraft dynamics
- wind and turbulence models
- sensor noise and bias models
- actuator failure injection
- scenario files
- binary log output
- HITL transport adapters

Those changes should keep `fsw_step()` as the shared execution boundary.

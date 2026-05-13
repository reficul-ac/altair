# Altair Simulation And Monte Carlo

Altair owns concrete host runners that bind Bayek to `altair_vehicle_interface()`. Altair also owns aircraft-specific simulation model values through `altair_fixedwing_sim_params()`. Bayek owns the reusable integration and dynamics helpers documented in [Bayek Simulation](../bayek/docs/simulation.md).

## SITL Plant

Altair currently uses Bayek's deterministic toy plant for `smoke` and an Altair-parameterized fixed-wing 6DOF plant for `cruise6dof`. These models cover:

- actuator lag
- actuator saturation through FSW and mixer outputs
- rough roll, pitch, and yaw-rate response
- simple airspeed and altitude evolution
- simulated IMU, GPS, baro, and airspeed samples

The fixed-wing model is a first-pass deterministic simulation contract, not a validated Altair flight dynamics model. Its parameters remain compile-time constants for this milestone. The plant exists to validate:

- build and link boundaries
- deterministic closed-loop stepping
- no NaN or Inf outputs
- bounded actuator commands
- repeatable CSV logging

CTest runs each `cruise6dof` command profile twice and compares the generated CSV files byte-for-byte. It also applies deliberately broad plausibility guardrails: finite values, bounded actuators, positive airspeed, reasonable altitude, bounded attitude/rates, and failsafe safe outputs for the failsafe profile. These thresholds are guardrails against broken sim behavior, not performance claims.

## Altair SITL Runner

`vehicle/sitl_runner.c` runs a fixed number of fixed-size steps. It initializes Bayek with `altair_vehicle_interface()`, prints CSV rows to stdout, and prints a timing summary to stderr.

CSV is used because it is transparent, dependency-free, easy to diff, and easy to consume from Python, spreadsheets, or CI artifacts.

The `smoke` scenario keeps its compact CSV output for plumbing checks. The `cruise6dof` scenario writes richer trajectory data including RC inputs, health/failsafe inputs, actuator outputs, latitude/longitude, NED position and velocity, Euler angles, attitude quaternion, body rates, airspeed, altitude, body acceleration, and the last force and moment vectors.

`cruise6dof` supports deterministic command profiles with `--profile`:

- `cruise`: hold the initial-condition RC command.
- `takeoff`: high throttle with a shallow climb command.
- `turn`: wings-level entry, positive roll command, then a small rollout command.
- `descent`: reduce throttle and command a shallow descent after the entry segment.
- `failsafe`: neutralize commands and inject an invalid GPS fix halfway through the run, exercising FSW failsafe safe outputs while the vehicle remains armed.

`cruise6dof` can start from a dependency-free initial-condition file:

```ini
# cruise6dof_initial.ini
lat_deg = 37.4275
lon_deg = -122.1697
altitude_m = 150
roll_rad = 0
pitch_rad = 0.04
yaw_rad = 0.1
vel_n_mps = 18
vel_e_mps = 0
vel_d_mps = 0
p_rps = 0
q_rps = 0
r_rps = 0
airspeed_mps = 18
rc_throttle = 0.6
rc_roll = 0
rc_pitch = 0.02
rc_yaw = 0
rc_arm = 1
rc_mode = 1
```

Each line is `key = value`; blank lines and `#` comments are ignored. Omitted fields use the scenario defaults. If no explicit NED velocity is provided, `airspeed_mps` is used as the initial forward speed.

Example run:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv
```

SITL runs as fast as possible by default. Add `--realtime` when a run should be paced so one simulated second takes one wall-clock second:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --realtime
```

Latitude and longitude in `cruise6dof` logs are computed from NED position with a local tangent-plane approximation around `lat_deg` and `lon_deg`. They are useful for plotting short local trajectories, not as a full geodesy model.

## SITL Replay CSV v1

The first committed replay fixture uses the existing `cruise6dof` CSV as the v1 replay contract. Its identity/configuration is:

- `scenario=cruise6dof`
- `profile=failsafe`
- `initial=tests/integration/cruise6dof_initial.ini`
- `duration=1.0`
- `dt=0.02`

The v1 fixture covers nominal flight followed by health degradation and failsafe output behavior. Required coverage is time, FSW mode, actuator outputs, RC inputs, explicit `gps_fix_valid`, local geodetic position, NED position and velocity, Euler attitude, attitude quaternion, body rates, airspeed, altitude, body acceleration, and the last body-frame force and moment vectors.

The v1 replay format is the exact CSV header below. Changing columns, order, names, or units requires regenerating `tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv`, updating this contract, and updating the replay test expected header/checks in `tests/integration/CMakeLists.txt`.

```text
step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,gps_fix_valid,lat_deg,lon_deg,pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,roll_rad,pitch_rad,yaw_rad,quat_w,quat_x,quat_y,quat_z,p_rps,q_rps,r_rps,airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2,force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm
```

Regenerate the v1 fixture with:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --profile failsafe --initial tests/integration/cruise6dof_initial.ini --duration 1.0 --dt 0.02 --output tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv
```

For routine local runs, `tools/python/run_sitl.py` wraps the compiled runner and prints key metrics from the generated CSV:

```sh
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --profile turn --initial cruise6dof_initial.ini --duration 20 --dt 0.01 --output sitl_turn6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --realtime
```

To run the usual end-to-end local workflow in one command, use `run_sitl_workflow.py`. Its defaults are the routine `cruise6dof` case, the repository initial-condition file, `duration=60`, `dt=0.01`, non-realtime execution, all plots saved under `plots/sitl`, and `sitl_3d.html` generation:

```sh
tools/python/run_sitl_workflow.py
```

The same script accepts overrides for less common runs:

```sh
tools/python/run_sitl_workflow.py --duration 10 --output /tmp/sitl.csv --plots-dir /tmp/plots --html /tmp/sitl_3d.html
tools/python/run_sitl_workflow.py --realtime
```

Expected output is a stable `key=value` summary that can be pasted into notes or checked in scripts:

```text
scenario=cruise6dof
output=sitl_cruise6dof.csv
rows=6000
end_time_s=59.990000
final_mode=1
final_airspeed_mps=...
final_altitude_m=...
finite=true
max_abs_roll_rad=...
min_motor=...
max_motor=...
```

The exact numeric values depend on duration, step size, initial conditions, and future model changes. `finite=true`, bounded motor output, and plausible final speed/altitude are the first checks to inspect.

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

- `run_sitl.py` invokes the compiled SITL runner and prints summary metrics.
- `run_sitl_workflow.py` runs SITL, plots the CSV log, and generates the standalone 3D playback page with routine defaults.
- `compare_sitl_replay.py` compares two replay CSV files with identical headers, identical row counts, and numeric tolerances.
- `run_mc.py` invokes the compiled runner and writes CSV.
- `plot_mc.py` prints simple summary statistics.
- `plot_sitl.py` plots selected `cruise6dof` trajectory views from a SITL CSV.
- `visualize_sitl_3d.py` writes a standalone browser playback page for `cruise6dof` trajectory CSV logs.

Example plotting commands:

```sh
python3 tools/python/plot_sitl.py sitl_cruise6dof.csv --plot velocities --plot attitudes --plot position --out-dir plots
python3 tools/python/plot_sitl.py sitl_cruise6dof.csv --plot all --show
python3 tools/python/visualize_sitl_3d.py sitl_cruise6dof.csv --output sitl_3d.html
```

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

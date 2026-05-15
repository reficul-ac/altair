# Altair Simulation And Monte Carlo

Altair owns concrete host runners that bind Bayek to `altair_vehicle_interface()`. Altair also owns aircraft-specific simulation model values in `params/sim/`, exposed to callers through `altair_fixedwing_sim_params()`. Bayek owns the reusable integration, generic sim parameter structs, dynamics helpers, fixed-wing trim helper, and host-only SITL parsing/condition machinery documented in [Bayek Simulation](../bayek/docs/simulation.md).

## SITL Plant

Altair currently uses Bayek's deterministic toy plant for `smoke` and an Altair-parameterized fixed-wing 6DOF plant for `cruise6dof`. These models cover:

- actuator lag
- actuator saturation through FSW and mixer outputs
- rough roll, pitch, and yaw-rate response
- simple airspeed and altitude evolution
- simulated IMU, GPS, baro, and airspeed samples

The fixed-wing model is a first-pass deterministic simulation contract, not a validated Altair flight dynamics model. Bayek's sim structs remain generic, while Altair's concrete physical values, including mass and wing area, are compile-time constants under `params/sim/` for this milestone. The plant exists to validate:

- build and link boundaries
- deterministic closed-loop stepping
- no NaN or Inf outputs
- bounded actuator commands
- repeatable CSV logging

CTest runs each `cruise6dof` command profile twice and compares the generated CSV files byte-for-byte. It also applies deliberately broad plausibility guardrails: finite values, bounded actuators, positive airspeed, reasonable altitude, bounded attitude/rates, and failsafe safe outputs for the failsafe profile. These thresholds are guardrails against broken sim behavior, not performance claims.

## Altair SITL Runner

`vehicle/sitl_runner.c` runs a fixed number of fixed-size steps. It initializes Bayek with `altair_vehicle_interface()`, prints CSV rows to stdout, and prints a timing summary to stderr.

The runner is still Altair-owned, but much of its reusable input machinery is now Bayek-owned. `bayek_host_sitl` provides the initial-condition parser and per-step condition engine; `bayek_sim` provides the fixed-wing trim helper. Altair keeps the concrete scenarios, command profiles, case-file defaults, CSV columns, MAVLink presentation, validation policy, and Python workflow defaults.

CSV is used because it is transparent, dependency-free, easy to diff, and easy to consume from Python, spreadsheets, or CI artifacts.

The `smoke` scenario keeps its compact CSV output for plumbing checks. The `cruise6dof` scenario writes richer trajectory data including RC inputs, health/failsafe inputs, actuator outputs, latitude/longitude, derived local NED position and velocity, Euler angles, attitude quaternion, body rates, airspeed, altitude, body acceleration, the last force and moment vectors, ECEF truth position/velocity, and mission status fields.

`cruise6dof` supports deterministic command profiles with `--profile`:

- `cruise`: hold the initial-condition RC command.
- `takeoff`: high throttle with a shallow climb command.
- `turn`: wings-level entry, positive roll command, then a small rollout command.
- `descent`: reduce throttle and command a shallow descent after the entry segment.
- `failsafe`: neutralize commands and inject an invalid GPS fix halfway through the run, exercising FSW failsafe safe outputs while the vehicle remains armed.
- `mission`: load a deterministic three-waypoint GPS mission that cruises level, then steps up to a higher-altitude waypoint.

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

`cruise6dof` defaults to ECEF truth dynamics. Select the legacy local-NED dynamics path with `--frame-mode ned`; use `--frame-mode ecef` explicitly when scripts should make the default visible. Internally this maps to Bayek's `SIM6DOF_FRAME_NED = 0` and `SIM6DOF_FRAME_ECEF = 1` parameter values. The current ECEF implementation uses a spherical Earth model with radius `6378137.0 m`; Earth rotation, Coriolis, centrifugal terms, and WGS84 ellipsoid corrections are out of scope for this pass.

The initial `lat_deg`, `lon_deg`, and `altitude_m` keys define both the ECEF initial geodetic state and the local NED origin used for compatibility columns. Explicit `vel_n_mps`, `vel_e_mps`, and `vel_d_mps` remain local NED velocity inputs and are converted to ECEF for ECEF-mode integration.

Example run:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv
```

SITL runs as fast as possible by default. Add `--realtime` when a run should be paced so one simulated second takes one wall-clock second:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --realtime
```

For live MAVLink telemetry, run `cruise6dof` with `--mavlink`. The runner sends MAVLink v1 `HEARTBEAT`, `ATTITUDE`, `GLOBAL_POSITION_INT`, and `VFR_HUD` messages to UDP `127.0.0.1:14550` by default. `--mavlink` implies realtime pacing because MAVLink consumers expect a live stream:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --mavlink
```

Use `--mavlink-host` and `--mavlink-port` for a remote endpoint or a non-default UDP port. The older `--qgc`, `--qgc-host`, and `--qgc-port` names remain compatibility aliases:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --mavlink --mavlink-host 127.0.0.1 --mavlink-port 14551
```

For simultaneous browser visualization and QGroundControl monitoring, use the live session launcher:

```sh
tools/python/run_sitl_session.py
```

The launcher starts `mavlink_live_bridge.py`, forwards raw packets to QGroundControl at `127.0.0.1:14550`, starts the browser live viewer at `http://127.0.0.1:5173`, and runs realtime `cruise6dof` SITL pointed at bridge UDP port `14551`. If the live viewer dependencies are not installed, rerun with `--install-viewer-deps`. Use `--no-qgc` or `--no-viewer` to disable either side of the live session.

Latitude, longitude, and altitude in `cruise6dof` logs are derived from the spherical-Earth ECEF truth state in ECEF mode. The local `pos_n_m`, `pos_e_m`, `pos_d_m`, `vel_n_mps`, `vel_e_mps`, and `vel_d_mps` columns remain available as derived compatibility outputs relative to the configured initial origin. The appended `pos_ecef_x_m`, `pos_ecef_y_m`, `pos_ecef_z_m`, `vel_ecef_x_mps`, `vel_ecef_y_mps`, and `vel_ecef_z_mps` columns expose the ECEF truth state directly.

## SITL Replay CSV v1

The first committed replay fixture uses the existing `cruise6dof` CSV as the v1 replay contract. Its identity/configuration is:

- `scenario=cruise6dof`
- `profile=failsafe`
- `initial=tests/integration/cruise6dof_initial.ini`
- `duration=1.0`
- `dt=0.02`

The v1 fixture covers nominal flight followed by health degradation and failsafe output behavior. Required coverage is time, FSW mode, actuator outputs, RC inputs, explicit `gps_fix_valid`, spherical geodetic position, derived NED position and velocity, Euler attitude, attitude quaternion, body rates, airspeed, altitude, body acceleration, the last body-frame force and moment vectors, ECEF truth position/velocity, and mission status.

The v1 replay format is the exact CSV header below. Changing columns, order, names, or units requires regenerating `tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv`, updating this contract, and updating the replay test expected header/checks in `tests/integration/CMakeLists.txt`.

```text
step,time_s,mode,motor,aileron,elevator,rudder,rc_throttle,rc_roll,rc_pitch,rc_yaw,gps_fix_valid,lat_deg,lon_deg,pos_n_m,pos_e_m,pos_d_m,vel_n_mps,vel_e_mps,vel_d_mps,roll_rad,pitch_rad,yaw_rad,quat_w,quat_x,quat_y,quat_z,p_rps,q_rps,r_rps,airspeed_mps,altitude_m,accel_x_mps2,accel_y_mps2,accel_z_mps2,force_x_n,force_y_n,force_z_n,moment_x_nm,moment_y_nm,moment_z_nm,pos_ecef_x_m,pos_ecef_y_m,pos_ecef_z_m,vel_ecef_x_mps,vel_ecef_y_mps,vel_ecef_z_mps,trim_active,trim_achieved,trim_failed,trim_iteration_count,trim_residual_norm,mission_loaded,mission_active_wp,mission_wp_count,mission_distance_m
```

Regenerate the v1 fixture with:

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --profile failsafe --initial tests/integration/cruise6dof_initial.ini --duration 1.0 --dt 0.02 --output tests/integration/fixtures/sitl_cruise6dof_failsafe_v1.csv
```

For routine local runs, `tools/python/run_sitl.py` wraps the compiled runner and prints key metrics from the generated CSV:

```sh
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --profile turn --initial cruise6dof_initial.ini --duration 20 --dt 0.01 --output sitl_turn6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --plot all --plots-dir plots/sitl
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --realtime
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --mavlink --mavlink-port 14551
```

`--plot` accepts `velocities`, `attitudes`, `rates`, `position`, `ecef`, or `all`. Static plots are saved to `--plots-dir`, which defaults to `plots/sitl` when plotting is requested.

For offline trajectory playback and analysis, use the browser/Electron live viewer. Start `tools/live_viewer`, select `Import`, and load the SITL CSV log. The viewer converts delimited CSV logs into deterministic replay frames for scrub/playback inspection.

For a live session with both the browser viewer and QGroundControl forwarding:

```sh
tools/python/run_sitl_session.py
tools/python/run_sitl_session.py --profile turn --duration 30
tools/python/run_sitl_session.py --no-qgc
```

Case files are an Altair one-time setup layer for initial conditions, run configuration,
vehicle parameters, sim parameters, and missions. The initial-condition data type and
parser are Bayek-owned and shared with the standalone `--initial` path. `cruise6dof`
also supports a separate per-step condition file through `[run] condition_file =
path/to/conditions.ini` in a case file or the CLI override `--conditions
path/to/conditions.ini`. Conditions are parsed by Bayek once and evaluated every
simulation step after the profile command and truth-derived FSW input are generated, but
before `altair_fsw_step()` and `sim_fixedwing_step()`.

Condition files use rule sections with a single v1 comparison over `t_s` or `step`:

```ini
[rule.gps_drop_after_30s]
when = t_s >= 30
input.gps.fix_valid = 0

[rule.param_stomp_after_20s]
when = step > 2000
vehicle_params.max_airspeed_mps = 7
```

Supported assignment prefixes are currently implemented by Bayek's host SITL condition
registry: `rc.*`, `input.*`, `vehicle_params.*`, `sim_params.*`, selected `plant.*`
fixed-wing/6DOF state fields, `trim.*`, and mission fields such as `mission.enabled`,
`mission.waypoint_count`, and `mission.waypoint.N.*`. Altair supplies the concrete
parameter values and runner policy those assignments act on.

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

The Monte Carlo summary CSV contract is:

```text
run,seed,scenario,passed,failure_reason,throttle_bias,final_airspeed_mps,final_altitude_m,max_abs_roll_rad
```

Each run is marked failed when any guardrail trips:

- `nonfinite_state`: final airspeed, final altitude, max roll, or plant state values are not finite.
- `unbounded_output`: FSW actuator output is non-finite or outside its normalized bounds.
- `invalid_mode`: FSW mode is outside disarmed, manual, stabilize, failsafe, or mission.
- `airspeed_limit`: final airspeed is outside `1.0..80.0 m/s`.
- `altitude_limit`: final altitude is outside `-100.0..10000.0 m`.
- `roll_limit`: max absolute roll exceeds `1.5708 rad`.

The runner exits `0` when all runs pass, `3` when any run fails a metric gate, and `1` for command-line or output-file errors. The current dispersions are placeholders. The key property is replay: the same seed and run count should produce the same CSV.

## Python Tools

Python scripts in `tools/python` are orchestration helpers only:

- `run_sitl.py` invokes the compiled SITL runner, prints summary metrics, and can save static plots.
- `run_sitl_session.py` runs the live workflow: MAVLink bridge, browser live viewer, optional QGroundControl forwarding, and realtime SITL.
- `mavlink_live_bridge.py` listens for MAVLink v1 UDP packets, forwards raw packets to one or more UDP endpoints such as QGroundControl, and publishes decoded live state over WebSocket for `tools/live_viewer`.
- `compare_sitl_replay.py` compares two replay CSV files with identical headers, identical row counts, and numeric tolerances.
- `plot_sitl.py` plots selected `cruise6dof` trajectory views from a SITL CSV.

Example plotting commands:

```sh
python3 tools/python/plot_sitl.py sitl_cruise6dof.csv --plot velocities --plot attitudes --plot position --out-dir plots
python3 tools/python/plot_sitl.py sitl_cruise6dof.csv --plot all --show
```

Monte Carlo post-processing remains direct CSV inspection. Run `./build/vehicle/mc_runner --seed 1 --runs 100 --scenario smoke --output mc_summary.csv` and inspect the generated summary with your preferred CSV tool.

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

Those changes should keep `altair_vehicle_interface()` and `altair_fsw_step()` as the shared execution boundary.

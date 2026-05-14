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

For simultaneous browser visualization and QGroundControl monitoring, route SITL through the live bridge. SITL sends to the bridge on `127.0.0.1:14551`; the bridge forwards the original packets to QGC on `127.0.0.1:14550` and publishes decoded state over WebSocket:

```sh
python3 tools/python/mavlink_live_bridge.py --listen-host 127.0.0.1 --listen-port 14551 --forward 127.0.0.1:14550 --ws-host 127.0.0.1 --ws-port 8765
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --mavlink --mavlink-port 14551
cd tools/live_viewer && npm install && npm run dev
```

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
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --realtime
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --mavlink --mavlink-port 14551
```

To run the usual end-to-end local workflow in one command, use `run_sitl_workflow.py`. Its defaults are the routine `cruise6dof` case, the repository initial-condition file, `duration=60`, `dt=0.01`, non-realtime execution, all plots saved under `plots/sitl`, and `sitl_3d.html` generation:

```sh
tools/python/run_sitl_workflow.py
```

The same script accepts overrides for less common runs:

```sh
tools/python/run_sitl_workflow.py --duration 10 --output /tmp/sitl.csv --plots-dir /tmp/plots --html /tmp/sitl_3d.html
tools/python/run_sitl_workflow.py --realtime
tools/python/run_sitl_workflow.py --mavlink --mavlink-port 14551
```

Case files are an Altair one-time setup layer for initial conditions, run configuration,
vehicle parameters, sim parameters, and missions. The initial-condition data type and
parser are Bayek-owned and shared with the standalone `--initial` path. `cruise6dof`
also supports a separate per-step condition file through `[run] condition_file =
path/to/conditions.ini` in a case file or the CLI override `--conditions
path/to/conditions.ini`. Conditions are parsed by Bayek once and evaluated every
simulation step after the profile command and truth-derived FSW input are generated, but
before `bayek_fsw_step()` and `sim_fixedwing_step()`.

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

## SITL Fixed-Wing Tuning Reference

Altair's first-pass fixed-wing model values are engineering estimates for deterministic
SITL, not measured or validated aircraft data. Tune them through `[sim_params]` in a
case file for one-time setup, or through `sim_params.*` assignments in a conditions file
for per-step experiments. The CSV schema does not include parameter trace columns; keep
the case or conditions file with the run output when parameter provenance matters.

| Parameter | Unit | Surface | Meaning and expected effect |
| --- | --- | --- | --- |
| `core.mass_kg` | kg | Flight-facing estimate | Vehicle mass. Higher mass slows acceleration and climb response. |
| `core.inertia_kgm2.x` | kg m^2 | SITL-only estimate | Roll inertia. Higher values slow roll-rate response. |
| `core.inertia_kgm2.y` | kg m^2 | SITL-only estimate | Pitch inertia. Higher values slow pitch-rate response. |
| `core.inertia_kgm2.z` | kg m^2 | SITL-only estimate | Yaw inertia. Higher values slow yaw-rate response. |
| `core.gravity_mps2` | m/s^2 | SITL-only environment | Gravity used by the 6DOF integrator. Normally leave at standard gravity. |
| `core.air_density_kgpm3` | kg/m^3 | SITL-only environment | Density used for dynamic pressure. Higher values increase lift and drag at the same airspeed. |
| `core.actuator_lag_hz` | Hz | SITL-only actuator model | First-order actuator response. Higher values track commands faster. |
| `core.frame_mode` | enum | SITL-only integration | `ecef` or `ned` in case files, numeric enum in conditions. Selects truth integration frame. |
| `core.earth_model` | enum | SITL-only integration | Spherical Earth model selector. Only `SIM6DOF_EARTH_SPHERICAL = 0` is currently valid. |
| `core.earth_radius_m` | m | SITL-only environment | Spherical Earth radius for ECEF/geodetic conversion. |
| `wing_area_m2` | m^2 | Flight-facing estimate | Reference wing area. Higher values increase lift and drag. |
| `wing_span_m` | m | Flight-facing estimate | Reference span for the fixed-wing parameter set. Currently documented and validated for ownership, with limited direct dynamics use. |
| `mean_chord_m` | m | Flight-facing estimate | Reference chord for the fixed-wing parameter set. Currently documented and validated for ownership, with limited direct dynamics use. |
| `max_thrust_n` | N | Flight-facing estimate | Full-throttle thrust. Higher values increase acceleration and climb energy. |
| `drag_cd0` | coefficient | SITL aero estimate | Baseline drag coefficient. Higher values reduce acceleration and steady speed. |
| `drag_cd_alpha` | coefficient/rad^2 | SITL aero estimate | Angle-of-attack drag growth. Higher values penalize pitch-up/high-alpha flight. |
| `lift_cl0` | coefficient | SITL aero estimate | Lift coefficient at zero effective angle of attack. Higher values increase level-flight lift. |
| `lift_cl_alpha` | coefficient/rad | SITL aero estimate | Lift slope versus angle of attack. Higher values increase pitch/lift sensitivity before stall limiting. |
| `lift_cl_elevator` | coefficient/command | SITL aero estimate | Elevator contribution to effective lift. Higher magnitude increases elevator authority in lift. |
| `stall_alpha_rad` | rad | SITL aero estimate | Effective angle-of-attack clamp. Lower values limit lift at smaller alpha. |
| `roll_aileron_nm` | N m/command | SITL control estimate | Aileron roll moment authority. Higher values increase roll acceleration. |
| `pitch_elevator_nm` | N m/command | SITL control estimate | Elevator pitch moment authority. Higher values increase pitch acceleration. |
| `yaw_rudder_nm` | N m/command | SITL control estimate | Rudder yaw moment authority. Higher values increase yaw acceleration. |
| `rate_damping_nms.x` | N m s/rad | SITL damping estimate | Roll-rate damping. Higher values damp roll rates more aggressively. |
| `rate_damping_nms.y` | N m s/rad | SITL damping estimate | Pitch-rate damping. Higher values damp pitch rates more aggressively. |
| `rate_damping_nms.z` | N m s/rad | SITL damping estimate | Yaw-rate damping. Higher values damp yaw rates more aggressively. |

Example case-file tuning block:

```ini
[sim_params]
core.mass_kg = 3.2
core.inertia_kgm2.x = 0.10
core.inertia_kgm2.y = 0.15
core.inertia_kgm2.z = 0.22
wing_area_m2 = 0.50
max_thrust_n = 16
drag_cd0 = 0.042
lift_cl_alpha = 4.5
roll_aileron_nm = 1.35
rate_damping_nms.y = 0.58
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

- `run_sitl.py` invokes the compiled SITL runner and prints summary metrics.
- `run_sitl_workflow.py` runs SITL, plots the CSV log, and generates the standalone 3D playback page with routine defaults.
- `mavlink_live_bridge.py` listens for MAVLink v1 UDP packets, forwards raw packets to one or more UDP endpoints such as QGroundControl, and publishes decoded live state over WebSocket for `tools/live_viewer`.
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

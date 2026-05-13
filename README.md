# Altair

Altair is the vehicle repository. Bayek is the reusable framework layer that Altair consumes as a private git submodule.

Design documentation lives in [docs/README.md](docs/README.md).

## Host Build

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the deterministic smoke SITL scenario and write a CSV flight log:

```sh
./build/vehicle/sitl_runner --scenario smoke --output sitl_smoke.csv
```

Run the fixed-wing 6DOF SITL scenario with an initial-condition file:

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
airspeed_mps = 18
rc_throttle = 0.6
rc_arm = 1
rc_mode = 1
```

```sh
./build/vehicle/sitl_runner --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --profile turn --initial cruise6dof_initial.ini --duration 20 --dt 0.01 --output sitl_turn6dof.csv
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --realtime
python3 tools/python/run_sitl.py --scenario cruise6dof --initial cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --qgc
python3 tools/python/plot_sitl.py sitl_cruise6dof.csv --plot velocities --plot attitudes --plot position --out-dir plots
python3 tools/python/visualize_sitl_3d.py sitl_cruise6dof.csv --output sitl_3d.html
```

`run_sitl.py` invokes the compiled runner and prints stable summary metrics such as `rows`, `final_airspeed_mps`, `final_altitude_m`, `finite`, `max_abs_roll_rad`, and motor command bounds.
For `cruise6dof`, `--profile` can select `cruise`, `takeoff`, `turn`, `descent`, or `failsafe` command profiles.
By default SITL runs as fast as the host can execute it. Pass `--realtime` to pace the run so one simulated second takes one wall-clock second.
Pass `--qgc` to stream `cruise6dof` attitude, global position, airspeed, and heartbeat MAVLink telemetry to QGroundControl over UDP. QGC listens on `127.0.0.1:14550` by default; use `--qgc-host` and `--qgc-port` to override that endpoint. `--qgc` implies realtime pacing.

`visualize_sitl_3d.py` writes a self-contained HTML playback page for `cruise6dof` CSV logs. Open the generated file in a browser to scrub and play back the 3D trajectory and aircraft attitude marker.

`cruise6dof` latitude and longitude columns use a local tangent-plane approximation around the initial latitude and longitude, not a full geodesy model.

Run the deterministic smoke Monte Carlo sweep and write a summary CSV:

```sh
./build/vehicle/mc_runner --seed 1 --runs 100 --scenario smoke --output mc_summary.csv
```

## Targets

- `bayek_common`: reusable C99 math, types, and control utilities.
- `bayek_fsw`: reusable flight software core with `bayek_fsw_init`, `bayek_fsw_reset`, and `bayek_fsw_step`.
- `bayek_sim`: deterministic toy plant helpers.
- `bayek_telemetry`: binary packet encode/decode helpers.
- `altair_vehicle`: Altair-specific parameters, limits, and mixer.
- `altair_sim_model`: Altair-owned fixed-wing sim model constants and validation around Bayek's reusable sim helpers.
- `sitl_runner`: deterministic fixed-step host simulation with CSV logging.
- `mc_runner`: deterministic Monte Carlo runner with CSV summary output.

Bayek must not include or link Altair-specific code. Altair binds Bayek through `altair_vehicle_interface()` and owns aircraft-specific sim model values; Bayek owns the reusable integration/dynamics helpers. The current fixed-wing model is deterministic and useful for guardrails, but it is not a validated flight dynamics model.

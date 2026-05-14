# Altair

Altair is the vehicle repository. Bayek is the reusable framework layer that Altair consumes as a private git submodule.

Design documentation lives in [docs/README.md](docs/README.md).

## Host Build

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Formatting

Altair-owned C, C++, CMake, and Python files are formatted with `clang-format`, `cmake-format`,
and `black`. Install those tools, then run check mode before opening a pull request:

```sh
python3 tools/python/format_repo.py --check
```

To rewrite tracked files locally:

```sh
python3 tools/python/format_repo.py --fix
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
tools/python/run_sitl_workflow.py
```

`run_sitl_workflow.py` is the offline playback workflow. It runs the default `cruise6dof` case, writes `sitl_cruise6dof.csv`, saves plots under `plots/sitl`, and generates `sitl_3d.html`.
For live QGroundControl and browser visualization, run a session:

```sh
tools/python/run_sitl_session.py
```

The session launcher starts the MAVLink bridge, forwards telemetry to QGroundControl at `127.0.0.1:14550`, starts the live viewer at `http://127.0.0.1:5173`, and runs realtime `cruise6dof` SITL through the bridge. If `tools/live_viewer/node_modules` is missing, rerun with `--install-viewer-deps`.

`run_sitl.py` remains the low-level wrapper around the compiled runner and prints stable summary metrics such as `rows`, `final_airspeed_mps`, `final_altitude_m`, `finite`, `max_abs_roll_rad`, and motor command bounds.
For `cruise6dof`, `--profile` can select `cruise`, `takeoff`, `turn`, `descent`, `failsafe`, or `mission` command profiles.
`cruise6dof` uses ECEF truth dynamics by default with a spherical Earth model and preserves local NED CSV columns as derived outputs. Use `--frame-mode ned` for the legacy local-NED dynamics path.
By default SITL runs as fast as the host can execute it. Pass `--realtime` to pace the run so one simulated second takes one wall-clock second.
Pass `--mavlink` to stream `cruise6dof` attitude, global position, airspeed, and heartbeat MAVLink telemetry over UDP. `--mavlink` defaults to `127.0.0.1:14550`, and `--mavlink-host`/`--mavlink-port` override that endpoint. The older `--qgc` names remain aliases.

`visualize_sitl_3d.py` writes a self-contained HTML playback page for `cruise6dof` CSV logs. Open the generated file in a browser to scrub and play back the 3D trajectory and aircraft attitude marker.

`cruise6dof` latitude, longitude, and altitude columns are derived from the spherical-Earth ECEF truth state. The CSV also appends ECEF position and velocity columns in meters and meters per second.

Run the deterministic smoke Monte Carlo sweep and write a summary CSV:

```sh
./build/vehicle/mc_runner --seed 1 --runs 100 --scenario smoke --output mc_summary.csv
```

Exit code `3` means at least one Monte Carlo run failed a metric gate.

## Targets

- `bayek_common`: reusable C99 math, types, and control utilities.
- `bayek_fsw`: reusable flight software core with `bayek_fsw_init`, `bayek_fsw_reset`, and `bayek_fsw_step`.
- `bayek_sim`: deterministic plant, 6DOF, fixed-wing, and trim helpers.
- `bayek_host_sitl`: host-only SITL initial-condition parsing and condition evaluation.
- `bayek_telemetry`: binary packet encode/decode helpers.
- `altair_vehicle`: Altair-specific parameters, limits, and mixer.
- `altair_sim_model`: Altair-owned fixed-wing sim model constants and validation around Bayek's reusable sim helpers.
- `sitl_runner`: deterministic fixed-step host simulation with CSV logging.
- `mc_runner`: deterministic Monte Carlo runner with CSV summary output.

Bayek must not include or link Altair-specific code. Altair binds Bayek through `altair_vehicle_interface()` and owns aircraft-specific sim model values, scenarios, CSV schemas, and runner policy; Bayek owns the reusable integration/dynamics helpers plus host-only SITL parsing and condition evaluation. The current fixed-wing model is deterministic and useful for guardrails, but it is not a validated flight dynamics model.

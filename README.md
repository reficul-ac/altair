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
python3 tools/python/plot_sitl.py sitl_cruise6dof.csv --plot velocities --plot attitudes --plot position --out-dir plots
```

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
- `sitl_runner`: deterministic fixed-step host simulation with CSV logging.
- `mc_runner`: deterministic Monte Carlo runner with CSV summary output.

Bayek must not include or link Altair-specific code. Altair binds Bayek through `altair_vehicle_interface()` and pins the framework source through the `bayek/` submodule.

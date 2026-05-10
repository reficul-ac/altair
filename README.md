# Altair

Altair is the vehicle repository. Bayek is the reusable framework layer that Altair consumes as a private git submodule.

Design documentation lives in [docs/README.md](docs/README.md).

## Host Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
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

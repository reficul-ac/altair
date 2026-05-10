# Altair

Initial C-first flight and simulation framework skeleton.

Design documentation lives in [docs/README.md](docs/README.md).

## Host Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Targets

- `altair_common`: reusable C99 math, types, and control utilities.
- `altair_vehicle`: Altair-specific parameters, limits, and mixer.
- `altair_fsw`: reusable flight software core with `fsw_init`, `fsw_reset`, and `fsw_step`.
- `sitl_runner`: deterministic fixed-step host simulation with CSV logging.
- `mc_runner`: deterministic Monte Carlo runner with CSV summary output.
- `altair_telemetry`: binary packet encode/decode helpers.

The flight core intentionally avoids dynamic allocation, formatting, Arduino APIs, simulator APIs, file I/O, and host-only dependencies.

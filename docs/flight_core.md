# Flight Core

The flight core lives in `framework/fsw` and exposes a minimal C API:

```c
void fsw_init(const vehicle_params_t *params);
void fsw_reset(void);
void fsw_step(const fsw_input_t *in, fsw_output_t *out);
```

## API Contract

`fsw_init()` binds the flight core to a vehicle parameter set. Passing `NULL` selects Altair defaults.

`fsw_reset()` resets controller and estimator state while preserving the selected parameter set.

`fsw_step()` consumes one complete input sample and writes one complete output sample. The caller owns both input and output storage.

The core currently keeps its runtime state in a static internal struct. That avoids dynamic allocation while keeping the public API simple. If multiple simultaneous FSW instances are required later, the natural extension is an explicit context API:

```c
void fsw_context_init(fsw_context_t *ctx, const vehicle_params_t *params);
void fsw_context_step(fsw_context_t *ctx, const fsw_input_t *in, fsw_output_t *out);
```

The current API can remain as a default singleton wrapper around that future context API.

## Boundary Rules

The flight core must not use:

- `malloc`, `free`, or heap-backed containers
- `printf`, `snprintf`, `FILE`, `fopen`, or file I/O
- Arduino headers or board APIs
- simulator APIs
- telemetry transport APIs
- nondeterministic time sources

Inputs, outputs, parameters, and common math utilities are passed in through plain C structs and functions.

## Modes

The initial modes are:

- `FSW_MODE_DISARMED`: command safe actuator values.
- `FSW_MODE_MANUAL`: pass normalized RC commands through the Altair mixer.
- `FSW_MODE_STABILIZE`: map RC sticks to simple attitude/rate setpoints and apply PID utilities.
- `FSW_MODE_FAILSAFE`: command safe actuator values.

The mode logic is intentionally simple. It uses the arm switch, GPS validity, and `dt_s` sanity bounds. This is not intended to be a final safety manager; it exists to make mode transitions deterministic and testable.

## Estimate Placeholder

The current estimate update integrates gyro rates into an attitude quaternion and copies simple sensor-derived values into a `state_estimate_t`. This is enough for deterministic replay, control loop wiring, and SITL smoke tests.

Future estimator work should be added behind the same input/output boundary. A complementary filter, EKF, or external estimator can replace the placeholder without changing host simulation, telemetry, or board shims.

## Stabilize Placeholder

The stabilize logic maps normalized RC input to:

- roll angle command
- pitch angle command
- yaw rate command

PID controllers generate normalized actuator requests that pass through the vehicle mixer. This is a placeholder control law. It validates architecture and determinism but does not claim aircraft performance.

## Determinism

Determinism is a first-class design constraint. `fsw_step()` depends only on:

- current input sample
- static FSW state initialized by `fsw_init()` and `fsw_reset()`
- selected vehicle parameters

This makes replay tests meaningful and makes host/embedded behavior easier to compare.

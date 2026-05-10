# Common Math And Control

`framework/common` contains shared primitives used by the vehicle layer, flight core, simulation, and tests.

## Types

`common_types.h` defines:

- `real_t`
- `vec2_t`, `vec3_t`, `mat3_t`
- `quat_t`, `euler_t`
- sensor samples
- RC input
- state estimate
- actuator commands
- FSW input/output
- vehicle parameters

`real_t` is currently `float`. This matches many embedded targets and keeps bandwidth and state size small. A later precision switch can be added, but the initial skeleton avoids that complexity.

## Math Utilities

`math_utils` provides:

- vector add, subtract, scale, dot, cross, norm, normalize
- quaternion identity, normalize, multiply, rotate
- Euler to quaternion conversion
- quaternion to Euler conversion
- angle wrap to `[-pi, pi]`
- clamp
- linear interpolation
- finite-value check

The math utilities are handwritten to avoid pulling in external dependencies during the initial skeleton. The functions are intentionally small and test-covered.

## Control Utilities

`control_utils` provides:

- PID with output limits
- explicit integrator with anti-windup limits
- first-order low-pass filter
- rate limiter
- direct slew limiter
- deadband
- actuator saturation helper

These utilities are generic. They know nothing about Altair, Arduino, SITL, telemetry, or flight modes.

## Rationale

Keeping these primitives in `framework/common` prevents the flight core from accumulating local one-off math. It also makes it possible to test numerical behavior separately from mode logic and vehicle mixing.

The utilities are deliberately conservative. They avoid hidden allocation and avoid callbacks or abstract interfaces. Future optimization can happen locally if profiling identifies a need.

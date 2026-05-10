# Vehicle Layer

The Altair vehicle layer lives under `vehicles/altair`.

## Files

- `altair_config.h`: control-rate and compile-time configuration constants.
- `altair_limits.h`: actuator limits.
- `altair_params.c/h`: default `vehicle_params_t`.
- `altair_mixer.c/h`: mapping from normalized control requests to actuator commands.

## Responsibilities

The vehicle layer is responsible for airframe-specific knowledge. That includes actuator ranges, safe actuator values, and mixer conventions.

The flight core should ask the vehicle layer to produce valid actuator commands rather than duplicating vehicle-specific saturation behavior.

## Mixer Design

The current mixer maps:

- throttle to motor
- roll command to aileron
- pitch command to elevator
- yaw command to rudder

Each channel is saturated to its declared range.

This is intentionally simple. More complex mixing can be added here without changing `fsw_step()` callers. Examples include elevon mixing, differential thrust, trim offsets, surface reversal, per-channel slew limits, or actuator health masking.

## Parameter Design

`altair_default_params()` returns a pointer to a static const parameter block. This avoids allocation and gives host and embedded builds the same default values.

If runtime parameter loading is needed later, it should occur outside `framework/fsw`. The loaded parameter struct can then be passed into `fsw_init()`.

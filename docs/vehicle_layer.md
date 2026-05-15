# Vehicle Layer

The Altair vehicle layer lives in root-level vehicle-owned folders.

## Files

- `config/altair_config.h`: control-rate and compile-time configuration constants.
- `mixer/altair_limits.h`: actuator limits.
- `params/altair_params.c/h`: default flight-facing `vehicle_params_t`.
- `params/sim/altair_sim_params.c/h`: Altair-specific SITL physical-model constants.
- `mixer/altair_mixer.c/h`: mapping from normalized control requests to actuator commands.
- `vehicle/altair_vehicle.c/h`: `bayek_vehicle_interface_t` implementation for Altair.
- `vehicle/sitl_case.c/h`: Altair-specific case-file defaults and adapters around Bayek's reusable SITL initial-condition and condition machinery.

## Responsibilities

The vehicle layer is responsible for airframe-specific knowledge. That includes actuator ranges, safe actuator values, mixer conventions, host-only simulation constants, concrete SITL scenarios, and presentation policy. Bayek owns reusable host SITL parsers and condition evaluation, but Altair decides which defaults, profiles, CSV columns, and validation gates are used for this aircraft.

Bayek FSW asks the configured vehicle interface to produce valid actuator commands rather than duplicating vehicle-specific saturation behavior.

## Mixer Design

The current mixer maps:

- throttle to motor
- roll command to aileron
- pitch command to elevator
- yaw command to rudder

Each channel is saturated to its declared range.

This is intentionally simple. More complex mixing can be added here without changing `altair_fsw_step()` callers. Examples include elevon mixing, differential thrust, trim offsets, surface reversal, per-channel slew limits, or actuator health masking.

## Parameter Design

`altair_default_params()` returns a pointer to a static const parameter block for values intended to be loaded by onboard flight software. `altair_vehicle_params_default()` copies the same compiled safe defaults into caller-owned storage. These defaults are always the fallback source when no file or runtime update is supplied.

Flight-facing GNC parameters and host-only simulation parameters are intentionally separate:

- `params/altair_params.c/h` owns `vehicle_params_t` defaults, validation, `[vehicle_params]` INI loading, and the runtime vehicle-parameter store.
- `params/sim/altair_sim_params.c/h` owns fixed-wing `sim_fixedwing_params_t` defaults, validation, `[sim_params]` INI loading, and the runtime sim-parameter store.

SITL-only physical model constants, such as fixed-wing mass and wing area, live under `params/sim/`. Host simulation callers continue to use `altair_fixedwing_sim_params()`, which delegates to those sim-only defaults. The current PlatformIO source filter includes `params/*.c` but not `params/sim/*.c`, so these sim-only values are kept out of the Arduino firmware build path.

Runtime parameter updates use a staged C API: begin from the active set, apply one or more named key/value updates, validate the complete staged set, then commit atomically. Invalid staged updates are rejected and the active set remains unchanged. V1 runtime changes are volatile only; there is no auto-save or save-on-command behavior.

`altair_vehicle_interface()` attaches the active `vehicle_params_t` storage to Bayek's generic vehicle interface. `altair_fsw_reset()` preserves the active parameter pointer so a reset does not silently discard runtime-loaded parameters.

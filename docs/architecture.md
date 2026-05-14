# Architecture

Altair is organized as a C-first vehicle repository. Bayek is the reusable framework layer consumed through the private `bayek/` git submodule. The layout keeps Bayek and Altair-specific source separate.

## Repository Layout

```text
bayek/
  common/      Shared C99 types, math, and control utilities.
  fsw/         Portable flight software core and internal FSW domains.
  sim/         Generic deterministic plant, 6DOF, fixed-wing, and trim helpers.
  host/        Vehicle-agnostic host SITL parsers and condition machinery.
  hitl/        Future hardware-in-the-loop adapters.
  telemetry/  Packet encode/decode helpers.
params/        Altair default parameters.
mixer/         Altair actuator limits and mixer.
config/        Altair compile-time configuration.
vehicle/       Altair-to-Bayek interface and Altair host runners.
boards/
  arduino_altair/  PlatformIO project and Arduino-compatible board shim.
tools/
  python/      Orchestration and plotting helpers only.
tests/
  unit/
  integration/
  performance/
```

## Dependency Direction

The intended dependency graph is:

```text
bayek/common
        ^
        |
bayek/fsw, bayek/sim, bayek/telemetry
        ^
        |
bayek/host
        ^
        |
altair_vehicle and Altair runners/tests/boards
```

Bayek must not know Altair exists. Files under `bayek/` must not include `altair_*` headers or link against `altair_vehicle`. Altair may depend on Bayek and provides vehicle-specific behavior through `altair_vehicle_interface()`.

`bayek/fsw` must remain portable. It should not include host I/O, simulator code, telemetry transport, Arduino APIs, dynamic allocation, file access, or Altair headers. That rule is more important than the current placeholder control law because it keeps the flight logic reusable on both host and embedded targets.

## Module Responsibilities

`bayek/common` owns reusable data types and primitive algorithms. These utilities should stay generic enough for any vehicle or board.

`params/`, `mixer/`, `config/`, and `vehicle/` own values and transformations that are specific to the Altair airframe. They are the right places for actuator limits, mixer mapping, default vehicle parameters, and the Bayek vehicle interface implementation. Bayek may produce normalized control requests, but Altair owns the final actuator policy.

`bayek/fsw` owns reusable portable flight domains. Altair owns the vehicle-level flight software loop through `altair_fsw_init()`, `altair_fsw_reset()`, and `altair_fsw_step()`, while Bayek provides the domain APIs:

- `nav`: state-estimate reset and update from sensor inputs.
- `fault`: input validity and mode/failsafe selection.
- `guidance`: mode-specific setpoint generation.
- `control`: controller state and normalized control requests.
- `mission`: waypoint mission state, validation, active-waypoint advancement, and mission setpoint selection.

Altair's FSW facade orchestrates those domains with vehicle-specific hooks for relative launch, external guidance, performance management, and final actuator mixing.

`bayek/sim` owns generic plant dynamics, state propagation, sensor-input helpers, fixed-wing dynamics helpers, and fixed-wing trim support. `bayek/host` owns host-only, vehicle-agnostic SITL parsing and condition machinery. Altair-specific SITL scenarios, Monte Carlo profiles, CSV outputs, runner CLIs, default case values, and presentation policy live under `vehicle/` and `tools/` because they bind Bayek to `altair_vehicle_interface()` and Altair-specific workflow policy.

`params/sim/` owns Altair's first-pass aircraft-specific fixed-wing simulation constants. It starts from Bayek's reusable fixed-wing defaults and applies Altair-only physical model values such as mass and wing area. `vehicle/altair_sim_model.c` keeps the SITL-facing `altair_fixedwing_sim_params()` API and validates the resulting sim parameter contract alongside flight-facing limits from `altair_default_params()`. Bayek remains responsible for generic sim structs, 6DOF integration, fixed-wing force/moment helper code, fixed-wing trim mechanics, and host SITL condition parsing; Altair owns concrete sim values, command profiles, CSV schemas, and guardrails. These parameters are compile-time constants for the current milestone.

`bayek/telemetry` owns binary packet formatting. It is independent of `altair_fsw_step()` so telemetry can be used by host tools, embedded transports, or HITL without coupling packet handling to control execution.

`boards/arduino_altair` owns Arduino setup/loop integration and hardware abstraction stubs.

Bayek module internals are documented in [Bayek Architecture](../bayek/docs/architecture.md).

## Build Model

CMake is the canonical host build path. Each module exposes a named target so future tests and tools can link only what they need. PlatformIO is limited to the Arduino-compatible embedded skeleton.

Clone Altair with submodules, or initialize Bayek after cloning:

```sh
git submodule update --init --recursive
```

Host build commands:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The code is C99, with C11-compatible style. The current scalar type is `real_t`, defined as `float`.

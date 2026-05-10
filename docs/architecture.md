# Architecture

Altair is organized as a C-first repository with a small reusable flight software core and host-side tooling around it. The layout is designed to make the boundary between portable flight logic and platform-specific code visible in the filesystem.

## Repository Layout

```text
framework/
  common/      Shared C99 types, math, and control utilities.
  fsw/         Portable flight software core.
  sim/         Host-only deterministic simulation plant and SITL runner.
  mc/          Host-only Monte Carlo runner.
  hitl/        Future hardware-in-the-loop adapters.
  telemetry/  Packet encode/decode helpers.
vehicles/
  altair/      Vehicle-specific params, limits, config, and mixer.
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
framework/common
        ^
        |
vehicles/altair
        ^
        |
framework/fsw

framework/sim, framework/mc, tests, and boards depend on the flight core.
The flight core does not depend on them.
```

`framework/fsw` must remain portable. It should not include host I/O, simulator code, telemetry transport, Arduino APIs, dynamic allocation, or file access. That rule is more important than the current placeholder control law because it keeps the flight logic reusable on both host and embedded targets.

## Module Responsibilities

`framework/common` owns reusable data types and primitive algorithms. These utilities should stay generic enough for any vehicle or board.

`vehicles/altair` owns values and transformations that are specific to the Altair airframe. It is the right place for actuator limits, mixer mapping, and default vehicle parameters.

`framework/fsw` owns flight-mode selection, caller-visible FSW state, state-estimate updates, and control commands. It consumes generic inputs and produces generic outputs.

`framework/sim` owns host-only plant dynamics, sensor generation, and SITL runner logic. It may use `stdio`, timing APIs, and other host facilities because those do not cross into FSW.

`framework/mc` owns deterministic batch execution and summary metrics. Python may orchestrate the compiled runner but should not become the core simulator.

`framework/telemetry` owns binary packet formatting. It is independent of `fsw_step()` so telemetry can be used by host tools, embedded transports, or HITL without coupling packet handling to control execution.

`boards/arduino_altair` owns Arduino setup/loop integration and hardware abstraction stubs.

## Build Model

CMake is the canonical host build path. Each module exposes a named target so future tests and tools can link only what they need. PlatformIO is limited to the Arduino-compatible embedded skeleton.

Host build commands:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The code is C99, with C11-compatible style. The current scalar type is `real_t`, defined as `float`.

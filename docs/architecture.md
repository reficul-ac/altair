# Architecture

Altair is organized as a C-first vehicle repository. Bayek is the reusable framework layer consumed through the private `bayek/` git submodule. The layout keeps Bayek and Altair-specific source separate.

## Repository Layout

```text
bayek/
  common/      Shared C99 types, math, and control utilities.
  fsw/         Portable flight software core.
  sim/         Generic deterministic toy plant helpers.
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
altair_vehicle and Altair runners/tests/boards
```

Bayek must not know Altair exists. Files under `bayek/` must not include `altair_*` headers or link against `altair_vehicle`. Altair may depend on Bayek and provides vehicle-specific behavior through `altair_vehicle_interface()`.

`bayek/fsw` must remain portable. It should not include host I/O, simulator code, telemetry transport, Arduino APIs, dynamic allocation, file access, or Altair headers. That rule is more important than the current placeholder control law because it keeps the flight logic reusable on both host and embedded targets.

## Module Responsibilities

`bayek/common` owns reusable data types and primitive algorithms. These utilities should stay generic enough for any vehicle or board.

`params/`, `mixer/`, `config/`, and `vehicle/` own values and transformations that are specific to the Altair airframe. They are the right places for actuator limits, mixer mapping, default vehicle parameters, and the Bayek vehicle interface implementation.

`bayek/fsw` owns flight-mode selection, caller-visible FSW state, state-estimate updates, and control commands. It consumes generic inputs, calls the configured vehicle interface, and produces generic outputs.

`bayek/sim` owns generic toy plant dynamics and sensor generation. Altair-specific SITL and Monte Carlo runners live under `vehicle/` because they bind Bayek to `altair_vehicle_interface()`.

`bayek/telemetry` owns binary packet formatting. It is independent of `bayek_fsw_step()` so telemetry can be used by host tools, embedded transports, or HITL without coupling packet handling to control execution.

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

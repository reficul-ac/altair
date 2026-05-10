# Design Rationale

This document records why the initial skeleton makes certain choices. The purpose is to help future changes preserve intent rather than copying the first implementation blindly.

## C First

The flight core is C99 because C has predictable ABI behavior, is easy to integrate into embedded projects, and avoids pulling C++ runtime assumptions into a small control loop. The Arduino shim is C++ only because Arduino uses C++ entry points.

## `real_t` Is `float`

`real_t` is currently `float` to match common microcontroller hardware and reduce memory bandwidth. The type alias makes a future precision experiment possible without rewriting every interface.

The project should not add mixed float/double APIs casually. If double precision is needed later, make it a deliberate configuration choice and rerun replay/performance tests.

## No Dynamic Allocation In FSW

The flight core avoids heap allocation to keep timing and failure modes predictable. Caller-owned structs and static internal state are easier to reason about on embedded targets.

If multiple instances are needed later, use caller-owned context structs rather than heap allocation.

## No Formatting Or File I/O In FSW

Formatting and file I/O introduce hidden dependencies, variable timing, and platform assumptions. Logging belongs in host runners, telemetry adapters, or board/HAL layers.

## Static Singleton FSW State

The first API uses a static singleton because it is small and easy for embedded callers. This is not meant to block future multi-instance simulation. The planned extension is an explicit `fsw_context_t` while keeping the singleton wrapper for simple targets.

## Vehicle Layer Between Common And FSW

The vehicle layer prevents Altair-specific actuator limits and mixer choices from leaking into generic math or host simulation. It also prevents the FSW core from hardcoding an airframe-specific actuator layout in multiple places.

## CMake For Host, PlatformIO For Arduino

CMake is used for host builds because it integrates naturally with CTest and CI. PlatformIO is used only where it adds value: compiling the Arduino-compatible board project.

The two build systems should share source files, not duplicate flight logic.

## CSV For Early Host Outputs

SITL and Monte Carlo write CSV because it is easy to inspect, diff, and consume without extra dependencies. Binary logs can be added later when data volume or fidelity justifies them.

## Toy Plant Instead Of High-Fidelity Dynamics

The current plant validates architecture, determinism, and bounded behavior. A high-fidelity model would take more assumptions than the skeleton currently has and could obscure whether the software boundaries are correct.

The plant should improve incrementally after the basic interfaces settle.

## Deterministic Monte Carlo

The Monte Carlo runner is deterministic by seed so failures can be reproduced exactly. This matters more than statistical richness in the initial skeleton.

Future dispersions should preserve replay by recording seeds and configuration in the CSV or an adjacent manifest.

## Telemetry Outside FSW

Telemetry is independent of the control step because packet formats and transports change more often than control logic. Keeping the boundary separate allows the same FSW outputs to be logged, transmitted, replayed, or ignored by different callers.

## Conservative Tests

The initial tests focus on simple invariants: math correctness, saturation, determinism, packet CRCs, and smoke execution. They do not overclaim flight performance.

As the control law and plant mature, tests should add scenario-level assertions and golden replay artifacts.

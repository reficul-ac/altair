# Design Rationale

This document records Altair-specific choices. Bayek framework rationale lives in [Bayek Design Rationale](../bayek/docs/design_rationale.md).

## Altair As Vehicle Repository

Altair owns the airframe-specific layer: parameters, actuator limits, mixer behavior, board integration, host runners, and vehicle-facing tests. Bayek owns reusable framework code.

This split keeps Bayek portable while letting Altair evolve as a concrete aircraft project.

## Bayek As Submodule

Bayek is pinned through the `bayek/` git submodule. This gives Altair reproducible builds while allowing framework changes to land in the Bayek repository first.

Altair code may include Bayek headers and link Bayek targets. Bayek code must not include Altair headers or link Altair targets.

## Root-Level Vehicle Folders

Altair-specific source lives in root-level folders:

- `params/`
- `mixer/`
- `config/`
- `vehicle/`

These folders make the vehicle-owned surface explicit and avoid hiding Altair code inside the Bayek submodule.

## Vehicle Interface Boundary

Altair binds Bayek through `altair_vehicle_interface()`. That function supplies Bayek with default parameters, manual mixing, control mixing, and safe actuator behavior.

Keeping this boundary explicit prevents the FSW core from hardcoding Altair actuator limits or mixer choices.

## CMake For Host, PlatformIO For Arduino

CMake is used for host builds because it integrates naturally with CTest and CI. PlatformIO is used only for the Arduino-compatible board project.

The two build systems should share source files and the same Bayek submodule revision, not duplicate flight logic.

## CSV For Altair Host Outputs

Altair SITL and Monte Carlo runners write CSV because it is easy to inspect, diff, and consume without extra dependencies. Binary logs can be added later when data volume or fidelity justifies them.

## Deterministic Monte Carlo

The Monte Carlo runner is deterministic by seed so failures can be reproduced exactly. Future dispersions should preserve replay by recording seeds and configuration in the CSV or an adjacent manifest.

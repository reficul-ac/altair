# Embedded And HAL

The embedded skeleton lives in `boards/arduino_altair`.

## PlatformIO Project

`platformio.ini` defines an Arduino-compatible environment and includes the same C source files used by host builds:

- `framework/common/*.c`
- `framework/fsw/*.c`
- `vehicles/altair/*.c`

This is the main embedded design point: board builds should link the same flight core source as host tests and simulation.

## Arduino Shim

`src/main.cpp` is intentionally thin:

1. initialize HAL
2. initialize FSW
3. read inputs
4. call `fsw_step()`
5. write actuators
6. send telemetry

The file may include Arduino APIs because it is a board-layer shim. Those APIs must not move into `framework/fsw`.

## HAL Stubs

`lib/altair_hal` provides C functions for:

- initialization
- time
- sensor and RC input
- actuator output
- telemetry transport

The current implementation is a stub. It returns disarmed, safe default inputs and ignores outputs. Real hardware integration should replace these functions while preserving the C boundary.

## Rationale

Separating the HAL from FSW keeps hardware drivers, pin mappings, serial transports, and Arduino framework assumptions out of portable control logic.

That makes it possible to compare behavior across:

- unit tests
- SITL
- Monte Carlo
- HITL
- Arduino-compatible embedded builds

All of those paths should exercise the same `framework/fsw` C files.

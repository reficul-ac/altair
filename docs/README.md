# Altair Design Documentation

This directory documents the initial Altair C flight/simulation framework. The current codebase is intentionally a deterministic skeleton rather than a high-fidelity aircraft or autopilot implementation. The design goal is to establish boundaries that remain useful as the project grows.

## Documents

- [Architecture](architecture.md): repository layout, dependency rules, and module responsibilities.
- [Flight Core](flight_core.md): `framework/fsw` API, state ownership, modes, estimator placeholder, and control flow.
- [Common Math And Control](common_math_control.md): reusable scalar, vector, quaternion, PID, filter, and limiter utilities.
- [Vehicle Layer](vehicle_layer.md): Altair-specific params, limits, and mixer responsibilities.
- [Simulation And Monte Carlo](simulation_and_mc.md): host SITL plant, deterministic replay, Monte Carlo runner, and CSV output.
- [Telemetry](telemetry.md): packet format, CRC design, topic ranges, and decode/encode boundaries.
- [Embedded And HAL](embedded.md): PlatformIO skeleton, Arduino shim, HAL stubs, and why embedded code stays outside FSW.
- [Testing Strategy](testing.md): unit, integration, performance, and determinism checks.
- [Design Rationale](design_rationale.md): reasons for key choices and planned extension points.

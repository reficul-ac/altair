# Altair Design Documentation

This directory documents Altair as a vehicle repository with Bayek as the reusable framework submodule. The current codebase is intentionally a deterministic skeleton rather than a high-fidelity aircraft or autopilot implementation. The design goal is to keep the Bayek and Altair boundaries explicit as both repos grow.

## Documents

- [Architecture](architecture.md): repository layout, dependency rules, and module responsibilities.
- [Vehicle Layer](vehicle_layer.md): Altair-specific params, limits, and mixer responsibilities.
- [Simulation And Monte Carlo](simulation_and_mc.md): Altair host runners on Bayek's reusable SITL harness, deterministic replay, Python helpers, and CSV output.
- [Embedded And HAL](embedded.md): PlatformIO skeleton, Arduino shim, HAL stubs, and why embedded code stays outside FSW.
- [Testing Strategy](testing.md): Altair vehicle, integration, and performance checks.
- [Design Rationale](design_rationale.md): reasons for Altair-specific choices.
- [Animus Architecture Plan](animus_architecture.md): consolidated Animus
  architecture, completed phases, module boundaries, and operator roadmap.

Bayek framework documentation lives in the submodule:

- [Bayek Architecture](../bayek/docs/architecture.md)
- [Bayek Common Math And Control](../bayek/docs/common_math_control.md)
- [Bayek Flight Core](../bayek/docs/flight_core.md)
- [Bayek Simulation](../bayek/docs/simulation.md)
- [Bayek Telemetry](../bayek/docs/telemetry.md)
- [Altair Telemetry Contract](telemetry_contract.md)
- [Bayek Testing Strategy](../bayek/docs/testing.md)
- [Bayek Design Rationale](../bayek/docs/design_rationale.md)

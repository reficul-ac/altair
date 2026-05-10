# Altair HITL

This directory is reserved for hardware-in-the-loop adapters. The flight core should remain independent of these files; HITL code should translate external sensor, actuator, and telemetry transports into the public `fsw_step()` interface.

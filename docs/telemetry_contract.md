# Altair Telemetry Contract

Altair owns the vehicle-specific telemetry contract for live SITL MAVLink, embedded
Bayek-envelope topics, and deterministic replay logs. Bayek owns only the reusable
packet envelope and topic-base rule documented in
[`bayek/docs/telemetry.md`](../bayek/docs/telemetry.md).

The machine-readable contract is [`telemetry_contract.json`](telemetry_contract.json).
Tests treat that manifest as the required packet/topic/group list.

## Live SITL MAVLink

`vehicle/sitl_runner.c --scenario cruise6dof --mavlink` emits MAVLink v1 UDP
packets. `--mavlink` preserves the existing realtime behavior and endpoint flags.

Required messages:

- `HEARTBEAT` at 1 Hz.
- `ATTITUDE` at the simulation step cadence.
- `GLOBAL_POSITION_INT` at the simulation step cadence.
- `GPS_RAW_INT` at the simulation step cadence.
- `VFR_HUD` at the simulation step cadence.
- `MISSION_CURRENT` at the simulation step cadence.
- `HOME_POSITION` at the simulation step cadence.
- `TERRAIN_REPORT` at the simulation step cadence.
- `SERVO_OUTPUT_RAW` at the simulation step cadence.

SITL uses truth state for attitude, geodetic position, NED velocity, airspeed,
climb, mission index, home position, and actuator outputs. Deterministic
placeholders are allowed only where the current plant has no real source:
`GPS_RAW_INT` reports `eph=100 cm`, `epv=150 cm`, and 10 satellites while the
SITL GPS fix is valid; `HOME_POSITION` reports the initial-condition position
with an identity quaternion; `TERRAIN_REPORT` reports a zero-height local datum,
100 m spacing, loaded count 1, pending count 0, and current height equal to
truth altitude above that datum.

`SERVO_OUTPUT_RAW` maps Altair actuator commands to deterministic PWM-like
values: motor `0..1` maps to `1000..2000`, and centered control surfaces `-1..1`
map to `1000..2000`.

## Embedded Bayek Envelope

Embedded Altair telemetry must use Bayek's reusable packet envelope without
teaching Bayek Altair schemas. Altair-specific topic IDs start at
`TELEMETRY_TOPIC_VEHICLE_BASE`:

- `1000 altair.heartbeat_status`
- `1001 altair.vehicle_state`
- `1002 altair.actuator_outputs`
- `1003 altair.navigation_mission_state`
- `1004 altair.environment_terrain_state`
- `1005 altair.health_fault_summary`

No board transport is introduced by this contract. A future board transport or
binary logger must use these topic groups or update this contract and its tests.

## Replay Log

Deterministic replay remains the `cruise6dof` CSV v1 contract in
[`simulation_and_mc.md`](simulation_and_mc.md#sitl-replay-csv-v1). The manifest
maps those columns into the same groups used by live and embedded telemetry:
heartbeat/status, vehicle state, actuator outputs, navigation/mission state,
environment/terrain state, and health/fault summary. Changing replay columns,
order, names, or units requires updating both the CSV v1 contract and
`telemetry_contract.json`.

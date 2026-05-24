# Altair Telemetry Contract

Altair owns the vehicle-specific telemetry contract for live SITL MAVLink, embedded
Bayek-envelope topics, and deterministic replay logs. Bayek owns only the reusable
packet envelope and topic-base rule documented in
[`bayek/docs/telemetry.md`](../bayek/docs/telemetry.md).

The machine-readable contract is [`telemetry_contract.json`](telemetry_contract.json).
It is generated from [`telemetry_contract_source.json`](telemetry_contract_source.json)
with [`tools/python/generate_telemetry_contract.py`](../tools/python/generate_telemetry_contract.py).

## Field States

Freshness timeout: `2.0 s`.

| State | Meaning |
| --- | --- |
| fresh | A supported field was updated within the freshness timeout. |
| stale | A supported field was last updated, but is older than the freshness timeout. |
| unsupported | The current source contract does not provide this field. |
| unknown | The source supports the field, but no sample has arrived yet. |

## Live SITL MAVLink

`vehicle/sitl_runner.c --scenario cruise6dof --mavlink` emits MAVLink v1 UDP
packets. `--mavlink` preserves the existing realtime behavior and endpoint flags.

| ID | Message | Rate | Group |
| --- | --- | --- | --- |
| 0 | HEARTBEAT | 1 Hz | heartbeat_status |
| 30 | ATTITUDE | simulation step | vehicle_state |
| 33 | GLOBAL_POSITION_INT | simulation step | vehicle_state |
| 24 | GPS_RAW_INT | simulation step | vehicle_state |
| 74 | VFR_HUD | simulation step | vehicle_state |
| 42 | MISSION_CURRENT | simulation step | navigation_mission_state |
| 242 | HOME_POSITION | simulation step | navigation_mission_state |
| 136 | TERRAIN_REPORT | simulation step | environment_terrain_state |
| 36 | SERVO_OUTPUT_RAW | simulation step | actuator_outputs |

Deterministic placeholders are allowed only where the current plant has no real
source. The generated JSON lists those placeholder values explicitly.

## Embedded Bayek Envelope

Embedded Altair telemetry must use Bayek's reusable packet envelope without
teaching Bayek Altair schemas. Altair-specific topic IDs start at
`TELEMETRY_TOPIC_VEHICLE_BASE`.

| ID | Topic | Group |
| --- | --- | --- |
| 1000 | altair.heartbeat_status | heartbeat_status |
| 1001 | altair.vehicle_state | vehicle_state |
| 1002 | altair.actuator_outputs | actuator_outputs |
| 1003 | altair.navigation_mission_state | navigation_mission_state |
| 1004 | altair.environment_terrain_state | environment_terrain_state |
| 1005 | altair.health_fault_summary | health_fault_summary |

No board transport is introduced by this contract. A future board transport or
binary logger must use these topic groups or update this contract and its tests.

## Replay Log

Deterministic replay remains the `cruise6dof` CSV v1 contract in
[`simulation_and_mc.md`](simulation_and_mc.md#sitl-replay-csv-v1). The replay
columns map into the same groups used by live and embedded telemetry.

| Group | Replay CSV Columns |
| --- | --- |
| heartbeat_status | step, time_s, mode, gps_fix_valid |
| vehicle_state | lat_deg, lon_deg, altitude_m, pos_n_m, pos_e_m, pos_d_m, vel_n_mps, vel_e_mps, vel_d_mps, roll_rad, pitch_rad, yaw_rad, quat_w, quat_x, quat_y, quat_z, p_rps, q_rps, r_rps, airspeed_mps, accel_x_mps2, accel_y_mps2, accel_z_mps2 |
| actuator_outputs | motor, aileron, elevator, rudder, rc_throttle, rc_roll, rc_pitch, rc_yaw, force_x_n, force_y_n, force_z_n, moment_x_nm, moment_y_nm, moment_z_nm |
| navigation_mission_state | mission_loaded, mission_active_wp, mission_wp_count, mission_distance_m |
| environment_terrain_state | pos_ecef_x_m, pos_ecef_y_m, pos_ecef_z_m, vel_ecef_x_mps, vel_ecef_y_mps, vel_ecef_z_mps |
| health_fault_summary | trim_active, trim_achieved, trim_failed, trim_iteration_count, trim_residual_norm |

Changing replay columns, order, names, or units requires updating the source
contract, regenerating these artifacts, and updating the replay fixture checks.

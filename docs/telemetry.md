# Telemetry

`framework/telemetry` provides reusable binary packet helpers. Telemetry is separate from `framework/fsw` so packet handling does not affect control-loop execution.

## Packet Format

The current packet layout is little-endian:

```text
byte 0      sync byte: 0xA5
bytes 1-2   topic_id
bytes 3-6   timestamp_us
bytes 7-8   sequence
bytes 9-10  payload_len
bytes 11-12 crc16
bytes 13..  payload
```

The maximum payload length is currently 128 bytes.

## CRC

The CRC is CRC-16/CCITT style with polynomial `0x1021` and initial value `0xffff`. During CRC computation the CRC field itself is zeroed.

This is simple, deterministic, and adequate for early packet validation. If the transport or safety requirements grow, the CRC and packet framing can be revised behind the same encode/decode API.

## Topics

Common topics include:

- heartbeat
- state
- actuator

Altair-specific topic IDs start at `1000`.

## Boundaries

Telemetry helpers do not call `fsw_step()` and the flight core does not call telemetry helpers. A board, host logger, or HITL adapter may choose when to encode FSW outputs or decode incoming commands.

This separation keeps telemetry bandwidth, packet format, and transport failures from becoming hidden dependencies inside control logic.

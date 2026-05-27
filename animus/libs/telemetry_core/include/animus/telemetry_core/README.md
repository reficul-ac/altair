# telemetry_core Public Headers

`animus::telemetry_core` owns deterministic offline telemetry playback data
structures and parsers. It is vehicle-agnostic and has no dependency on Altair,
Bayek, rendering, UI, terrain streaming, Python tooling, or live network ingest.

Current contracts:

- `TelemetrySample`, `Entity`, `Track`, `Event`, `Timeline`, and
  `PlaybackClock` are the public playback model.
- `load_tlog()` and `load_tlog_bytes()` parse offline MAVLink `.tlog` bytes.
- `load_mcap_protobuf()` parses MCAP channels using the canonical
  `animus.telemetry.v1.TelemetrySample` Protobuf schema.
- `load_hdf5()` parses the canonical `/animus/telemetry/v1/samples` HDF5 table.
- `load_telemetry()` dispatches by explicit `TelemetryImportFormat`.
- MAVLink v1 and unsigned MAVLink v2 frames are accepted.
- Signed MAVLink v2 frames are rejected and counted in parser diagnostics.
- Common playback messages decoded today are `HEARTBEAT`,
  `GLOBAL_POSITION_INT`, `GPS_RAW_INT`, `ATTITUDE`, and `VFR_HUD`.
- Unsupported MAVLink messages are preserved as timeline events only after the
  frame is structurally parseable.
- Malformed, truncated, unsupported-version, signed-v2, and CRC-failed input is
  reported through `ParserDiagnostics`.
- Structured imports also report schema mismatches, unsupported channels or
  layouts, decode failures, skipped records, non-monotonic timestamps, and
  missing required time/position fields through `ParserDiagnostics`.
- Untimed messages attach to the most recent timestamped message for their
  entity when possible; otherwise they use time zero.

The canonical HDF5 v1 layout is intentionally narrow: `/animus/telemetry/v1`
must carry `schema = "animus.telemetry.v1.TelemetrySample"` and `version = 1`
attributes, and `/animus/telemetry/v1/samples` is a two-dimensional numeric
table. Columns are timestamp, system id, component id, latitude, longitude, MSL
altitude, relative altitude, roll, pitch, yaw, ground speed, climb rate,
heading, altitude datum, field-validity mask, and timestamp-valid flag.

Live UDP telemetry is not part of this module phase. It remains a later ingest
path that should normalize into the same `Timeline` model.

Terrain-relative placement is intentionally app-owned. `telemetry_core` reports
positions and altitude metadata; `apps/animus` combines those samples with
resident `terrain_core` height rasters when drawing overlays.

# telemetry_core Public Headers

`animus::telemetry_core` owns deterministic offline telemetry playback data
structures and parsers. It is vehicle-agnostic and has no dependency on Altair,
Bayek, rendering, UI, terrain streaming, Python tooling, or live network ingest.

Current Phase K contracts:

- `TelemetrySample`, `Entity`, `Track`, `Event`, `Timeline`, and
  `PlaybackClock` are the public playback model.
- `load_tlog()` and `load_tlog_bytes()` parse offline MAVLink `.tlog` bytes.
- MAVLink v1 and unsigned MAVLink v2 frames are accepted.
- Signed MAVLink v2 frames are rejected and counted in parser diagnostics.
- Common playback messages decoded today are `HEARTBEAT`,
  `GLOBAL_POSITION_INT`, `GPS_RAW_INT`, `ATTITUDE`, and `VFR_HUD`.
- Unsupported MAVLink messages are preserved as timeline events only after the
  frame is structurally parseable.
- Malformed, truncated, unsupported-version, signed-v2, and CRC-failed input is
  reported through `ParserDiagnostics`.
- Untimed messages attach to the most recent timestamped message for their
  entity when possible; otherwise they use time zero.

Terrain-relative placement is intentionally app-owned. `telemetry_core` reports
positions and altitude metadata; `apps/animus` combines those samples with
resident `terrain_core` height rasters when drawing overlays.

# Animus Developer Workspace

Animus uses Dear ImGui inside the native OpenGL app. The terrain viewport is the
primary surface; UI chrome should stay compact, contextual, and app-owned under
`apps/animus`.

## Current Shell

- Status bar: telemetry state, tile residency, frame time, active source, and
  recording state.
- Navigation: `View`, `Layers`, `Telemetry`, `Capture`, and `Developer`.
- Inspector: appears when terrain, layer, telemetry source, or entity context is
  active.
- Timeline: appears only for offline telemetry playback. Live telemetry follows
  the latest received sample and does not show a scrubber.

## User-Facing Controls

- `View`: camera/zoom status, terrain pack identity, state color toggle, and
  fallback highlighting.
- `Layers`: overlay enable/opacity, layer counts, bathymetry state, GeoTIFF,
  MBTiles, and remote imagery source status.
- `Telemetry`: live/offline source state, playback controls for logs, entity
  selection, terrain-height warnings, and event visibility filters.
- `Capture`: manual PNG capture and MP4 recording controls.

## Developer-Only Diagnostics

Developer mode owns details that are useful for implementation and performance
work but should not dominate the default app:

- render stats, GL vendor/renderer/version, upload budgets, and resident GPU
  memory
- cache L0/L1/L2/L3 counters, synthesis counts, persistence, and GeoTIFF
  extraction failures
- parser diagnostics, unsupported/skipped records, live UDP queue state, dropped
  datagrams/samples, and live ingest/copy/draw timing
- tile runtime table with state, source, cache tier, priority, fallback parent,
  synthetic depth, height range, and error text

The `Show diagnostics` checkbox keeps these views visually contained even after
Developer mode is selected.

## Telemetry Event Filters

Telemetry event filters are app-local UI state. Informational events are hidden
by default because unsupported or packet-level MAVLink events can be dense
during live sessions. Warning and error events remain visible by default.

## CLI And Config Links

- `--debug-overlay` enables the ImGui shell. It remains the default.
- `--no-debug-overlay` disables all ImGui chrome for deterministic terrain-only
  capture and smoke workflows.
- `--capture-ppm`, `--capture-png`, `--capture-sequence-dir`, and
  `--capture-sequence-fps` run capture/export paths without requiring UI
  interaction.
- `--telemetry`, `--telemetry-tlog`, `--playback-rate`, and
  `--playback-start-paused` configure offline telemetry playback.
- `--telemetry-live-udp HOST:PORT`, `--telemetry-live-buffer-s`,
  `--telemetry-live-max-samples`, `--telemetry-live-render-max-points`, and
  `--telemetry-live-debug-csv` configure live MAVLink ingest and diagnostics.
- `--overlay`, `--overlay-opacity`, and `--overlay-order` configure the primary
  GeoTIFF overlay. Persisted app config may restore these values when config
  loading is enabled.

## Phase O Direction

Phase O keeps Dear ImGui and the current native architecture. The design goal is
a terrain-first app shell: quiet status, compact mode navigation, contextual
inspection, and progressive disclosure of developer detail. Future UI framework
changes should wait until this native shell proves the structure, workflows, and
state model.

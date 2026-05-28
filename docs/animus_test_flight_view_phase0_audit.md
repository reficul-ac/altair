# Animus Test Flight View Phase 0 Audit

This audit records the current state before implementing the Test Flight View
roadmap in `docs/animus_architecture.md`. It is intentionally documentation
only: no app config, plot, signal catalog, UI, telemetry, render, terrain, or
vehicle feature code is introduced in Phase 0.

Baseline verification before this audit passed:

```bash
python3 animus/tools/verify_animus.py
```

Result: Conan/CMake completed and CTest reported 109/109 Animus tests passing.

## Architecture Boundaries

- `apps/animus` currently owns the UI shell, operator/developer workspace
  policy, selected entity behavior, layer controls, capture controls, app
  config hook, vehicle visual fallback policy, and terrain-relative placement.
- `telemetry_core` owns deterministic telemetry models, MAVLink frame parsing,
  reducers, imports, playback clocks, parser diagnostics, and timeline events.
  It remains vehicle-agnostic.
- `telemetry_live` owns UDP receive, queued datagrams, dropped datagram stats,
  bounded live history, live parse/reduce, and render-thread draining into the
  app.
- `render_core`, `terrain_core`, and `vehicle_core` should not gain Test Flight
  View UI policy. `vehicle_core` only owns reusable descriptor validation and
  CPU-side asset loading.
- New Phase 1-3 histories must be bounded. Plot or MAVLink observation history
  must not grow without both time-window and sample-count limits.
- Animus must remain a terrain/test-flight viewer. Phase 1-3 should not add
  command/control UI or vehicle write-back paths.

## Current Config Behavior

Current files:

- `animus/apps/animus/src/options.hpp`
- `animus/apps/animus/src/options.cpp`
- shutdown save call in `animus/apps/animus/src/animus_app.cpp`

Current config path behavior:

- `Options::config_path` defaults empty, so no default config path is loaded or
  saved today.
- `--config PATH` sets `config_path`.
- `--no-load-config` sets `load_config = false`.
- The parser performs a first CLI scan for only `--config` and
  `--no-load-config`, calls `load_app_config(options)`, then performs the full
  CLI parse. This means explicit CLI flags after config load override loaded
  values for the same launch.
- `--config` and `--no-load-config` are also accepted during the full second
  pass, but the second pass does not trigger another load. A later `--config`
  token only changes the eventual save path.

Current parser/writer style:

- The app config is JSON-like text, not YAML.
- Parsing is hand-rolled with substring searches for quoted top-level fields,
  floats, ints, bools, and a shallow `overlays` array of objects.
- Unknown keys are ignored implicitly.
- Invalid `workspace_mode` and `view_mode` values print warnings and keep the
  current/default value.
- Malformed JSON-like syntax is not reported as a single config error; missing
  recognized fields simply leave defaults in place.
- `save_app_config()` writes directly to `config_path` using `std::ofstream`.
  It creates the parent directory if needed, but it is not atomic and does not
  use temp-file/rename semantics.
- Save occurs once at app shutdown, not every frame. The saved options copy is
  updated with `ui_state.workspace_mode` and `ui_state.view_mode` before save.

Recognized loaded fields today:

- `cache_root`
- `pack_root`
- `overlay_geotiff`
- `geoid_grid`
- `overlay_opacity`
- `overlays`
- `workspace_mode`
- `view_mode`

Fields written today:

- `schema`
- `pack_root`
- `cache_root`
- `overlay_geotiff`
- `overlay_opacity`
- `overlay_order`
- `geoid_grid`
- `overlays`
- `workspace_mode`
- `view_mode`
- `debug_overlay`
- `capture_path`
- `capture_sequence_dir`
- `capture_sequence_fps`

Notable limitations versus the planned YAML config:

- No default `$XDG_CONFIG_HOME/animus/animus.yaml` or `~/.config/animus/animus.yaml`
  path.
- No YAML parser or versioned YAML schema. The current `schema` string is
  written but not validated on load.
- No config load/save status object for the UI.
- No dirty tracking, explicit Save, Save As Default, Reload, or Reset workflow.
- No atomic save.
- No persistence for panel visibility, map orientation, camera preset,
  follow-selected, selected entity, layer toggles beyond overlay config,
  timeline/bookmarks, thresholds, live UDP preferences, or plot definitions.

Recommended Phase 1 precedence:

1. Start with built-in safe defaults.
2. Resolve the config path from `--config PATH` when present, otherwise the
   platform default path.
3. Skip load when `--no-load-config` is present.
4. Load config values with warnings for unknown or invalid keys.
5. Apply explicit CLI flags last for this launch.
6. Save only intentional user preferences through an explicit save path or
   shutdown policy, preserving CLI override semantics without accidentally
   persisting runtime-only counters.

Candidate Phase 1 integration points:

- Keep CLI parsing in `options.cpp`, but move broader config serialization into
  a dedicated app-owned config module such as `apps/animus/src/app_config.*`.
- Initialize persistent UI preferences where `UiState` is created in
  `run()` before the main loop.
- Save from a controlled app-owned point near the existing shutdown
  `save_app_config(saved_options)` call, plus future explicit Settings actions.
- Avoid writing from per-frame UI draw code.

Risks:

- The two-pass CLI scan currently means `--config A --config B` loads A but
  saves B. Phase 1 should make path selection deterministic before load.
- Direct writes can corrupt the config if the app exits during save.
- Hand-rolled parsing will become fragile for nested preferences and plot
  definitions.

## App Runtime UI State

Current storage points:

- `UiState` in `animus/apps/animus/src/ui.hpp` holds app-local UI/runtime state.
- `TelemetryPlaybackState` in `ui.hpp` holds playback/live telemetry UI state
  and live diagnostics copied from `telemetry_live`.
- `InputState`, `Camera`, and `Map2DCamera` in `animus_app.cpp` hold camera and
  input state.
- `PlanVisualizationState`, `ScreenshotToolState`, and `Mp4RecorderState` hold
  app-local plan/capture state.

State fields that are good Phase 1 preference candidates:

- `UiState::workspace_mode`
- `UiState::view_mode`
- `UiState::active_mode`
- `UiState::developer_diagnostics_visible`
- `UiState::telemetry_diagnostics_visible`
- `UiState::telemetry_tracks_visible`
- `UiState::telemetry_labels_visible`
- `UiState::bathymetry_enabled`
- `UiState::follow_selected_entity`
- `TelemetryEventFilters`
- selected entity id, if available on relaunch and explicitly treated as a
  preference
- `Map2DCamera::orientation`
- stable camera preset or map/terrain view defaults
- layer preset choices, overlay enabled state, and overlay opacity
- plan overlay visibility and recent plan path, if that remains an app
  preference

State that must stay runtime-only:

- one-shot request flags such as fit, center, home, zoom, jump-latest, and
  review-jump requests
- live receiver stats, live buffer stats, frame timing, live ingest/prune/copy
  timings, rendered trail point counts, and batch counters
- `TelemetryPlaybackState::timeline`, runtime events, tracks, samples, and
  parser diagnostics
- current playback clock time unless a future workflow explicitly persists a
  review session profile
- `selected_entity_terrain`, terrain confidence under selected entity, and
  terrain clearance because they depend on current telemetry and resident tiles
- pending screenshot/recording flags and frame counts
- transient map-tool context points and terrain probes unless a future session
  bookmark feature explicitly owns them

Timeline and bookmarks:

- Offline playback uses `PlaybackClock` and `TimelineReviewData`.
- User bookmarks are currently `UiState::timeline_bookmarks`.
- Timeline review data is rebuilt from telemetry, bookmarks, and clearance
  samples. Persisting bookmarks would require a session/replay identity, not
  only global app preferences.

## Telemetry Flow

Current deterministic model:

- `TelemetrySample` carries `time_s`, entity id, latitude, longitude, optional
  MSL altitude, optional relative altitude, optional roll/pitch/yaw, optional
  ground speed, optional climb rate, optional heading, altitude datum, and
  source field-validity flags.
- `Timeline` carries samples, entities, tracks, events, parser diagnostics,
  source format, start time, and end time.

Current MAVLink parse:

- `parse_mavlink_stream()` accepts MAVLink v1 and unsigned MAVLink v2 frames.
- Signed MAVLink v2 frames are rejected and counted.
- CRC is checked for supported message ids.
- Unsupported versions, malformed frames, truncated frames, CRC failures,
  signed-v2 frames, unsupported messages, and decoded frames are counted in
  `ParserDiagnostics`.
- Unsupported message ids are emitted as `MavlinkMessage` entries with payloads
  after structural parsing, but they do not get CRC validation because no
  `crc_extra` is known.

Current decoded/reduced MAVLink messages:

- `HEARTBEAT` id 0: preserved as an info event when reducer message events are
  enabled.
- `GPS_RAW_INT` id 24: time, lat/lon, MSL altitude.
- `ATTITUDE` id 30: time, roll, pitch, yaw.
- `GLOBAL_POSITION_INT` id 33: time, lat/lon, MSL altitude, relative altitude,
  horizontal ground speed from vx/vy, climb rate, heading.
- `VFR_HUD` id 74: ground speed, heading, climb rate.

Raw decoded field retention:

- Raw MAVLink field values are not retained today.
- `telemetry_core::MavlinkMessage` retains only message metadata plus raw
  payload bytes during parse/reduce.
- The reducer copies a narrow set of numeric values into `PartialState` and
  emits `TelemetrySample`; raw fields, min/max values, field update times, and
  per-field histories are discarded.
- Live mode does not expose the parsed `MavlinkMessage` batch after
  `LiveTelemetryBuffer::ingest()` returns.

Live UDP flow:

- `UdpMavlinkReceiver` owns the UDP socket and an Asio thread.
- Received datagrams are timestamped with receiver-relative steady time and
  queued behind a mutex.
- The receive queue is bounded by `max_queued_datagrams = 4096`; overflowing
  drops the oldest datagram and increments `dropped_datagrams`.
- The render loop calls `live_receiver->drain()`, which swaps the queue into a
  local vector and records queue-before-drain and drain count.
- The render thread calls `live_buffer->ingest(datagrams)`.
- `LiveTelemetryBuffer` parses datagrams with `telemetry_core`, reduces them
  with receive-time fallback for untimed messages, suppresses per-message
  events, prunes by `history_seconds` and `max_samples`, and exposes stats.
- The app snapshots `live_buffer->timeline()` into
  `TelemetryPlaybackState::timeline` when new samples arrive and at least 0.05 s
  elapsed since the previous snapshot, or when the app has no samples yet.

Live counters and diagnostics:

- Receiver stats: datagrams, bytes, receive errors, dropped datagrams,
  queued datagrams, queue high water, last drain datagrams, last drain queue
  size, last packet age, connected, stale.
- Buffer stats: datagrams, bytes, dropped samples, parsed messages, produced
  samples, last batch datagrams/messages/samples, retained samples, ingest
  timing, prune/finalize timing, and parser diagnostics.
- App state also records live snapshot copy time, live overlay draw time,
  rendered trail point count, and per-frame batch message/sample counts.

## Plot Feed Candidates For Phase 2/3

Current plottable `TelemetrySample` fields:

- `time_s`
- `lat_deg`
- `lon_deg`
- `altitude_msl_m`
- `altitude_relative_m`
- `roll_rad`
- `pitch_rad`
- `yaw_rad`
- `ground_speed_mps`
- `climb_rate_mps`
- `heading_deg`
- source validity flags as non-numeric availability/status inputs

Safe first plot feed:

- Phase 2 can define an app-owned signal catalog over `TelemetrySample` and
  selected derived/runtime values without changing live receive behavior.
- Phase 3 strip plots can update from the render-thread timeline snapshot or
  from bounded app-owned series buffers after live drain.

Recommended bounded MAVLink observation-store hook:

- Add a bounded observation store in app-owned code, or in `telemetry_core` only
  if kept vehicle-agnostic and reusable.
- Feed it on the render thread while processing the already-drained datagram
  batch, not inside the UDP receive callback.
- If raw field-level access is needed, expose a `telemetry_core` decoder that
  maps supported `MavlinkMessage` payloads to numeric fields without retaining
  app UI state.
- Store latest numeric values plus bounded per-field histories keyed by
  system/component/message/field. Bound by both time window and max samples per
  field or globally.
- Do not lock the UDP receive thread on UI data structures.
- Do not parse every field into strings each frame.
- Do not let plot rendering block datagram drain, live reduction, or terrain
  rendering.

Decision for Phase 2/3:

- `TelemetrySample` fields are available now and should be the first signal
  catalog source.
- Basic MAVLink field inspector/plot support needs new raw-field observation
  storage because raw decoded fields are not retained today.
- The lowest-risk integration point is immediately after render-thread
  `live_receiver->drain()` and before or alongside `live_buffer->ingest()`.
  This sees raw datagram bytes without touching the receiver thread, and can
  reuse `telemetry_core` parsing/field decoders.

## Vehicle Descriptor YAML Handling

Current files:

- `animus/libs/vehicle_core/include/animus/vehicle_core/vehicle_definition.hpp`
- `animus/libs/vehicle_core/src/vehicle_definition.cpp`

Current behavior:

- Vehicle packages are discovered by scanning child directories under the
  vehicle root for `vehicle.animus.yaml`.
- Descriptor parsing is a small line-oriented YAML subset: comments are
  stripped, top-level `section:` headings are tracked, one indentation level is
  flattened to `section.key`, values may be single- or double-quoted, and
  unknown fields are ignored.
- Required fields are `id`, `display_name`, `type`, `model.path`,
  `model.scale`, `orientation.yaw_deg`, `orientation.pitch_deg`,
  `orientation.roll_deg`, `dimensions.length_m`, `dimensions.wingspan_m`, and
  `dimensions.height_m`.
- `type` currently supports `rc_plane`.
- `model.path` must reference a `.glb`; missing model files are warnings, not
  descriptor rejection.
- Duplicate ids are rejected.

Phase 1 implication:

- This descriptor parser is not a general YAML implementation and should not be
  reused as the broad app config parser unless Phase 1 intentionally accepts the
  same constrained subset. App preferences remain app-owned; vehicle descriptors
  remain in `vehicle_core`.

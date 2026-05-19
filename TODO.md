# Altair Active Backlog

This file tracks near-term repository work. Roadmap and design context live in:

- [Architecture](docs/architecture.md)
- [Vehicle layer](docs/vehicle_layer.md)
- [Simulation and Monte Carlo](docs/simulation_and_mc.md)
- [Testing](docs/testing.md)
- [Animus operator controls](docs/animus_operator_controls.md)

Reusable Bayek framework follow-up is tracked separately in
[`bayek/TODO.md`](bayek/TODO.md).

## Baseline Health

- [ ] Keep CI green for formatting, Debug/Release CMake, CTest, and Qt Animus.
- [ ] Keep generated build, CSV, plot, and Animus capture artifacts out of the worktree.

## Vehicle And SITL

- [ ] Replace placeholder Altair parameters with a documented first-pass airframe model.
- [ ] Document mass, wing area, speed limits, control limits, safe actuator positions, and aero data provenance.
- [x] Expand mixer tests for edge cases, saturation, sign conventions, and failsafe/disarmed outputs.
- [ ] Improve state estimation beyond direct placeholder field assignment.
- [ ] Wire Bayek wind, turbulence, sensor noise, and bias hooks into Altair SITL
  and Monte Carlo scenarios after the reusable hooks exist.
- [ ] Add randomized Monte Carlo inputs for Altair-owned dispersions, guardrails,
  and summary CSV semantics.
- [ ] Document Altair aero data provenance before replacing scalar aero formulas
  with reusable Bayek aero database helpers.

## Telemetry And Replay

- [ ] Define required telemetry packets for SITL, embedded, and log replay.
- [x] Add Altair replay schema compatibility checks around imported telemetry
  and live viewer session snapshots.
- [ ] Add log-to-replay tooling for deterministic regression tests.

## Animus

- [x] Update stale Animus docs and agent guidance that still reference the
  retired TypeScript app, `tools/animus`, npm workflows, browser launch, or
  `capture_animus_sitl.py`; point them to `tools/animus-qt`,
  `build-animus-qt`, and `capture_animus_qt_sitl.py`.
- [ ] Complete the remaining Animus Qt migration work: offline PMTiles serving,
  mission/fence/rally overlays, and broader Qt parity.
- [x] Record the exact upstream QGroundControl revision, license headers, and
  source-to-Animus mapping in `docs/animus_qgc_map_audit.md` before importing
  or adapting QGC-derived map/provider code.
- [x] Finish the 2D map controls: operator tile source selection, pan controls,
  scale/status UI, and richer offline failure states.
- [x] Make the QGC-style offline tile cache the only Animus Qt 2D raster path:
  operator-selected bounds/zoom levels, provider selection, async
  download/cancel, import/export/delete actions, cache metadata, and
  per-tile runtime state.
- [ ] Add a real Altair-local QtLocation `QGroundControl` provider plugin after
  Qt 6 Location is available in the target environment.
- [x] Add a cache-populated Animus Qt capture fixture that verifies real raster
  tile rendering while keeping fresh-checkout captures valid with the schematic
  empty-cache fallback.
- [x] Replace the static 3D terrain canvas placeholder with a WebEngine terrain
  workspace, Qt WebChannel vehicle/home/trail/terrain snapshots, and
  deterministic fallback/error UI.
- [x] Vendor full CesiumJS assets with license notices and complete
  quantized-mesh terrain rendering from `map_cache/terrain/quantized-mesh`.
- [x] Replace the Terrain 3D DOM/canvas terrain stand-in with native Cesium
  heightmap fixture terrain, offline raster imagery, bundled aircraft model,
  vehicle/home/trail primitives, and Cesium canvas screenshot capture.
- [x] Add richer terrain clearance analysis in Terrain 3D, including vehicle
  clearance trend/history and operator-visible guardrail thresholds.
- [ ] Add typed Qt models and QML overlays for mission items, geofences, rally
  points, home/origin, event markers, and breadcrumb/history layers.
- [x] Author or import `generic_fixed_wing_smooth.glb` with named
  control-surface pivot nodes.
  - Context: The first profile references a GLB asset that must provide the
    named pivot nodes before moving surfaces can render.
  - Suggested location/files:
    `tools/animus-qt/web/cesium/models/generic_fixed_wing_smooth.glb`.
  - Acceptance: Terrain 3D loads the GLB and every profile surface resolves to
    a glTF node with a hinge-aligned pivot.
- [x] Wire vehicle model profile loading into the Cesium scene.
  - Context: The profile JSON exists as configuration, but the scene still
    needs runtime model/profile selection.
  - Suggested location/files: `tools/animus-qt/web/cesium/animus-cesium.js`,
    `tools/animus-qt/web/cesium/vehicleModel.js`.
  - Acceptance: Terrain 3D loads the selected profile asset when present and
    falls back deterministically when it is missing.
- [x] Drive Cesium control-surface node rotations from CesiumBridge snapshots.
  - Context: The JS controller can apply node rotations, but live snapshot
    state still needs a non-neutral actuator-backed deflection source.
  - Suggested location/files: `tools/animus-qt/src/maps/CesiumBridge.cpp`,
    `tools/animus-qt/web/cesium/animus-cesium.js`.
  - Acceptance: A snapshot with nonzero `deflectionDeg` visibly rotates the
    matching named GLB nodes without breaking fallback rendering.
- [x] Parse `ACTUATOR_OUTPUT_STATUS` and/or `SERVO_OUTPUT_RAW` into actuator
  state.
  - Context: Animus currently decodes core vehicle and terrain telemetry, not
    actuator outputs needed for control-surface visualization.
  - Suggested location/files: `tools/animus-qt/src/telemetry/MavlinkDecoder.*`,
    `tools/animus-qt/src/telemetry/TelemetryService.*`.
  - Acceptance: Unit tests cover decoded normalized and/or PWM actuator output
    fields from MAVLink frames.
- [x] Add vehicle-specific channel mapping and polarity configuration.
  - Context: Control-surface polarity and channel assignment are
    vehicle/profile policy, not renderer policy.
  - Suggested location/files: `tools/animus-qt/web/cesium/models/*.json`,
    future Animus vehicle profile model code.
  - Acceptance: A vehicle profile can map actuator outputs to surface
    deflections with per-surface polarity and limits.
- [x] Add Setup tab UI for selecting model profile and validating/reversing
  surface polarity.
  - Context: Operators need a safe way to select the visual model and verify
    that surfaces move in the expected direction.
  - Suggested location/files: `tools/animus-qt/qml/SetupView.qml`, future
    profile/model Qt adapter.
  - Acceptance: Setup exposes model/profile selection plus per-surface polarity
    validation without hard-coding autopilot conventions.
- [x] Add test/fixture coverage for neutral and deflected control-surface
  snapshots.
  - Context: Neutral snapshot scaffolding is easy to regress once live actuator
    state is added.
  - Suggested location/files: `tools/animus-qt/tests/test_map_models.cpp`.
  - Acceptance: Tests cover neutral, positive, and negative deflection snapshot
    values and validity flags.
- [x] Add screenshot or semantic assertions for visible control-surface
  deflection in Terrain 3D.
  - Context: Node rotation must be verified visually or semantically in the
    WebEngine/Cesium scene.
  - Follow-up: Feed non-neutral `SERVO_OUTPUT_RAW` into the Animus Qt capture
    workflow or add an equivalent WebEngine semantic assertion so the screenshot
    bundle proves visible deflection, not just model/fallback health.
  - Suggested location/files: `tools/python/capture_animus_qt_sitl.py`,
    `tools/animus-qt/web/cesium/vehicleModel.js`.
  - Acceptance: Capture or JS-side assertions fail when a known deflected
    surface does not move from its neutral matrix.
- [ ] Add multi-vehicle support: per-system vehicle models, fleet
  list/selection, per-vehicle trails/status, and graceful degradation toward
  the documented 12-vehicle analysis target.
- [ ] Build Qt-native Flight, Dashboard, Inspector, Replay, Plan, Setup
  readiness, and guarded-command workflows only for features still wanted from
  the retired UI, with capture/test coverage for each workspace.
- [ ] Extend the first-pass Animus telemetry/link diagnostics beyond packet
  counters, decode errors, link freshness, MAVLink system/component identity,
  armed state, GPS, mission, home, and terrain summaries: add firmware/mode
  labels, battery/readiness summaries, packet age by message family, update
  rates, jitter, dropped/decoded counts, clock/source timestamps where
  available, and per-field stale/unsupported/unknown states.
- [ ] Move UDP receive/decode, map tile IO, and terrain/cache work
  behind explicit worker boundaries so rendering, synchronous tile reads,
  and map loading cannot contend with telemetry ingestion.
- [x] Replace Animus Qt capture readiness artifacts with real map-2d,
  terrain-3d, and setup screenshots plus nonblank scene checks.
- [ ] Add QtLocation-backed Animus Qt map rendering once CI and operator
  installs have a portable runtime package strategy; Ubuntu 24.04 repositories
  available on this host do not provide Qt 6 Location.
- [x] Fix the default 2D Animus map startup so a usable map source loads, or an
  explicit operator-facing offline/cache state is shown when it cannot.
- [x] Keep 2D vehicle snap/follow mode active across zoom changes; only unsnap
  when the operator explicitly disables follow mode or pans the 2D map.
- [x] Add a compact top-right flight telemetry strip for both 2D and 3D views
  with attitude, altitude, latitude/longitude, velocity, and link/state
  freshness.
- [ ] Add an energy-state and fixed-wing flight-envelope monitor for Flight,
  2D, and 3D views: airspeed, groundspeed, altitude AGL/MSL, vertical speed,
  attitude/load limits, stall/overspeed margins, climb/sink trend, and explicit
  unknown states when required telemetry is absent.
- [ ] Add navigation-quality and estimator-health panels for Setup and Flight
  views: GPS fix type, satellite count, HDOP/VDOP or covariance where
  available, EKF/estimator flags, position/velocity innovations, compass/IMU
  health, and degraded navigation warnings.
- [ ] Add route-progress and mission-execution awareness: active waypoint,
  cross-track error, distance/time to next waypoint, distance/time to home,
  mission completion estimate, loiter/hold state, and clear no-mission or
  unsupported-mission states.
- [ ] Add terrain and clearance awareness beyond the current terrain
  placeholder: AGL estimate, terrain report age, home-relative altitude,
  projected clearance along recent or active path, terrain-data availability,
  and caution states when terrain and altitude references disagree.
- [x] Improve Terrain 3D capture camera framing or add a full-workspace capture
  so the artifact shows terrain/clearance overlays, not only the native Cesium
  canvas and aircraft against mostly sky.
- [ ] Add wind and environment awareness for GNC debugging: estimated wind
  vector, headwind/crosswind components relative to track or mission leg,
  airspeed-vs-groundspeed consistency, and SITL weather/noise provenance during
  simulated sessions.
- [ ] Add control-authority and actuator-margin diagnostics: normalized
  roll/pitch/yaw/throttle demand, servo or actuator outputs, saturation flags,
  trim bias, RC override/manual input visibility, and failsafe/disarmed output
  state.
- [ ] Add an operator event timeline shared by live and replay views: mode
  changes, arming state, failsafe transitions, GPS/estimator degradation,
  command attempts, mission item changes, telemetry dropouts, and user-added
  markers.
- [ ] Add field-flight readiness profiles for Setup: preflight, launch,
  mission, recovery, and postflight check groups that summarize link, GPS,
  estimator, battery, mission, terrain, parameters, and log-recording readiness
  without enabling live writes by default.
- [ ] Audit available Altair/Bayek/MAVLink telemetry for fields that should be
  first-class 2D/3D overlays versus fields better exposed through custom
  operator widgets. Include passive fields from `SYS_STATUS`, `BATTERY_STATUS`,
  `POWER_STATUS`, `VFR_HUD`, `WIND_COV`, `NAV_CONTROLLER_OUTPUT`,
  `LOCAL_POSITION_NED`, `SERVO_OUTPUT_RAW`, `RC_CHANNELS`, `ESTIMATOR_STATUS`,
  `EKF_STATUS_REPORT`, and `STATUSTEXT`, and keep GNC-derived calculations
  centralized in Qt C++ model/helper code.
- [ ] Add operator-built custom telemetry widgets that can subscribe to selected
  MAVLink messages/fields and define simple math/comparison-driven status
  displays.
- [ ] Automatically fetch and display active mission plan/waypoint data needed
  by route-progress views when Altair/Bayek or the connected vehicle exposes
  mission data.
- [ ] Add UI to view and upload current flight parameters for the connected
  vehicle, using the existing Altair/Bayek parameter workflow where applicable.
- [ ] Automatically record received telemetry to a `.tlog` during each Animus
  session; persist the operator-selected save path, and default to the current
  repo when unset. TODO: revisit the default save location before hardware use.
- [ ] Add Animus dark/light mode switching with persisted operator preference.
- [ ] Replace the Terrain 3D placeholder with a true x/y/z or
  latitude/longitude/altitude trajectory view comparable to the Hawkeye 3D
  trajectory workflow.
- [x] Replace the current minimal bundled Terrain 3D aircraft GLB with a
  polished generic fixed-wing RC model.
  - Runtime format is GLB/glTF; OBJ is allowed only as an
    authoring/import/export intermediate.
  - Acceptance: Bundled GLB looks like a recognizable RC fixed-wing aircraft
    in Terrain 3D.
  - Acceptance: GLB preserves named pivot nodes for `aileron_left_pivot`,
    `aileron_right_pivot`, `elevator_pivot`, and `rudder_pivot`.
  - Acceptance: Existing `generic_fixed_wing_smooth.json` or its replacement
    profile maps all movable surfaces without renderer-side policy.
  - Acceptance: Animus Qt capture verifies the model loads and required pivot
    nodes move under the semantic control-surface diagnostic.
  - Acceptance: Asset provenance/license is documented before commit.
- [ ] Add local aircraft model import for Terrain 3D after the operator file
  policy, supported formats, and offline asset packaging are defined.
- [x] Add a tactical 3D attitude tab focused on aircraft attitude, angular
  rates, compass heading, and gyro-ring style roll/pitch/yaw visualization
  without trajectory clutter.
  - Acceptance: Tactical capture passes only through native Cesium/WebEngine
    GLB diagnostics with the selected profile asset loaded, vehicle-locked
    tactical camera state, and real deflected control-surface pivot movement;
    the QML fallback silhouette is allowed only as degraded live UI.
- [x] Refine Tactical Cesium visuals to use close-in RGB attitude-axis rings
  on a black background without the yellow reference line or terrain clutter.
- [x] Make Tactical `Snap` reset to the canonical isometric tactical camera
  even when the view is already in tactical camera mode.
- [x] Isolate Terrain 3D and Tactical WebEngine scene status and renderer state
  so tab switching cannot push either workspace into the other's fallback mode.
- [x] Add an FPV tab that renders the flight from the vehicle nose/seeker
  perspective.
- [ ] Port still-desired retired dashboard, inspector, replay import/export,
  flight view, and guarded command workflows into Qt Animus with Qt-native
  tests and capture coverage.
- [x] Extend Animus Qt screenshot analysis beyond nonblank PNG checks to catch
  obvious overlap, clipping, and workspace selection regressions.
- [x] Restore visible Animus Qt workspace tabs/header in live and capture runs;
  recent screenshots show the 2D/3D/setup selector missing while map content
  starts at the top of the window.
- [x] Tighten Animus Qt screenshot analysis to require actual workspace tab
  label visibility, not just nonblank top-region pixels.
- [x] Make `capture_animus_qt_sitl.py` retry or fall back to
  `xvfb-run`/offscreen when `xvfb-run` assigns a display that Qt/xcb cannot
  initialize.
- [ ] Diagnose Terrain 3D trackpad event delivery in Qt WebEngine and add a
  non-laggy Space+trackpad rotate path without a full-scene QML input overlay.
- [ ] Harden the Animus Qt local HTTP tile downloader test so sandbox loopback
  listen/download behavior is reported separately from cache-manager regressions.
- [x] Retire the obsolete TypeScript Animus app, generated dependency tree,
  interaction harnesses, and old CI lane now that Qt Animus is canonical.

## Test Ownership

- [ ] Keep Altair tests focused on Altair params, mixer, vehicle interface, SITL runners/cases/conditions, telemetry integration, and Animus.
- [ ] Keep only minimal Altair-side Bayek contract coverage needed to protect integration assumptions.
- [x] Add path-aware verification selection so agent and human checks can map changed files to the smallest defensible command set.
- [ ] Build richer non-Animus visual verification reports that combine metrics,
  plots, logs, and manifests in one reviewable artifact.

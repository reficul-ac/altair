# Animus Bloat And Intentional UI Audit

This audit reviews the current Qt Animus interface for UI and workflow clutter.
It is documentation-only: no behavior is removed here. Recommendations that
change operator workflow or hide existing information should be treated as
design approval items before implementation.

Animus remains the canonical Qt/QML operator shell under `tools/animus-qt/`.
Reusable telemetry, map, and simulation framework behavior remains in Bayek only
when it is vehicle-agnostic; this audit does not recommend Bayek changes.

## Audit Progress

Use this section as the source of truth for the behavior-preserving Animus UI
declutter sequence. Keep `TODO.md` at backlog level until the broader pass is
complete.

### Sequence Checklist

Shared primitives:

- [x] Add shared QML overlay, status badge, icon button, and segmented-control
  primitives under `tools/animus-qt/qml/`.
- [x] Register shared primitives in the Animus Qt QML module.
- [x] Migrate low-risk duplicated surfaces first: telemetry strip frame,
  terrain clearance/status overlays, Terrain 3D camera modes, and FPV/Tactical
  reset controls.
- [x] Add follow-up primitives only when a second concrete use exists, such as
  diagnostics drawers, setup sections, or telemetry summaries.

Global chrome disclosure:

- [x] Move mock telemetry, UDP, theme, and capture-oriented controls behind a
  compact diagnostics/settings disclosure while preserving current actions.
- [x] Keep link freshness and command authority visible in the primary chrome.

Map 2D cleanup:

- [x] Convert primitive text controls for pan, zoom, recenter, disclosure, and
  close into shared compact controls with tooltips.
- [x] Collapse repeated provider/cache wording into a concise status plus
  expanded details without hiding offline/licensing state.

Scene debug-status cleanup:

- [x] Hide healthy WebEngine, fixture, and renderer readiness sentences from
  the primary layer while keeping degraded states explicit.
- [x] Keep scene status, fallback, and capture diagnostics in artifacts/logs.

Setup rework:

- [x] Reorganize Setup into task sections: Readiness, Telemetry Link, Vehicle
  Model, Maps And Terrain, Logs, and Diagnostics.
- [x] Collapse raw paths, IDs, node names, cache DB names, and polarity internals
  behind details by default.

Visual refresh:

- [x] Reduce repeated borders and equal-weight panels after behavior is
  centralized.
- [x] Run full before/after screenshot comparison against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`.

### Completed Work

- 2026-05-24: Added `AnimusTelemetrySummary` and moved Map 2D, Terrain 3D, and
  FPV onto the same glanceable telemetry overlay. The primary row now leads with
  link, GPS/nav, MSL altitude, groundspeed, vertical speed, and attitude
  validity, while raw position and compact heading/altitude/velocity details
  remain available by expanding the summary. Terrain 3D and FPV now use a
  clearance-first scene cue with AGL, recent minimum clearance, and trend as the
  headline; camera/source/route details move behind disclosure except for
  degraded clearance states. The header settings popup is now a right-side
  diagnostics drawer for telemetry controls, theme, scene readiness, and
  capture state. Terrain 3D gained reference/clearance cues above the WebEngine
  scene, and FPV gained cockpit-style flight-path cues around the existing
  attitude cue. Existing read-only authority, link freshness, capture hooks,
  semantic object names, and WebEngine/Cesium diagnostics remain intact.
  Verification: `python3 tools/python/format_repo.py --check`, `cmake -S . -B
  build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-animus-qt --target animus_qt animus_qt_unit_tests
  --parallel`, `ctest --test-dir build-animus-qt --output-on-failure -R
  animus_qt`, and `python3 tools/python/capture_animus_qt_sitl.py` passed.
  Screenshot artifact: `artifacts/animus-qt-screenshots/20260524T235020Z/`.
  Compared against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`; mean pixel
  deltas were `7.19` (Map 2D), `3.15` (Terrain 3D), `10.23` (Terrain 3D
  workspace), `7.74` (FPV), `7.75` (FPV workspace), `31.10` (Tactical), and
  `29.29` (Tactical workspace). No capture or chrome diagnostic failures were
  found. Terrain workspace diversity improved over the immediately preceding
  run after the reference cue fix, while the report still retains WebEngine
  native-scene flatness warnings for FPV/Tactical and low-diversity warnings for
  Terrain/Tactical scene subregions.
- 2026-05-24: Calmed the shared Animus chrome after the behavior-preserving
  declutter slices by moving workspace tab styling into `AnimusWorkspaceTab`
  and refreshing `AnimusSegmentedControl`, `AnimusIconButton`,
  `AnimusStatusBadge`, and `AnimusOverlayPanel` with quieter default borders,
  clearer active indicators, and stable compact sizing. The header still keeps
  link freshness and read-only command authority visible, while telemetry,
  UDP, mock, and theme controls remain behind the existing settings
  disclosure. Existing workspace tab object names, capture hooks, chrome
  diagnostics, overlay diagnostics, and operator actions remain intact.
  Verification: `python3 tools/python/format_repo.py --check`, `cmake -S . -B
  build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-animus-qt --target animus_qt animus_qt_unit_tests
  --parallel`, `ctest --test-dir build-animus-qt --output-on-failure -R
  animus_qt`, and `python3 tools/python/capture_animus_qt_sitl.py` passed.
  Screenshot artifact: `artifacts/animus-qt-screenshots/20260524T215243Z/`.
  Compared against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`; mean pixel
  deltas were `10.1` (Map 2D), `1.0` (Terrain 3D), `7.1` (FPV), `26.8`
  (Tactical), `12.0` (Setup), and `17.1` (seeded Map 2D). No new capture
  failures or chrome diagnostic failures were found; the report retains the
  existing low-diversity warnings for WebEngine/Cesium scene captures.
- 2026-05-24: Reworked Setup from an implementation property sheet into
  task-oriented `Readiness`, `Telemetry Link`, `Vehicle Model`, `Maps And
  Terrain`, `Logs`, and `Diagnostics` sections using a shared
  `AnimusSetupSection` disclosure wrapper. First-layer summaries now emphasize
  link freshness, armed/GPS/mission/home/terrain state, selected model health,
  map policy, provider availability, tile-cache status, and current logging
  availability, while endpoint details, packet counters, MAVLink IDs, profile
  IDs, GLB paths, node names, polarity internals, cache paths, and tile-set IDs
  stay available through collapsed details. Existing model profile, polarity,
  map policy/provider, tile download/cancel/delete, seed/create, and reload
  actions remain wired. Verification: `python3 tools/python/format_repo.py
  --check`, `cmake -S . -B build-animus-qt
  -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug`, `cmake --build
  build-animus-qt --target animus_qt animus_qt_unit_tests --parallel`, `ctest
  --test-dir build-animus-qt --output-on-failure -R animus_qt`, and
  `python3 tools/python/capture_animus_qt_sitl.py` passed. Screenshot artifact:
  `artifacts/animus-qt-screenshots/20260524T195831Z/`. Compared Setup against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/setup.png`;
  the mean pixel delta was `12.5`. No new capture failures were found; the
  report retains existing low-diversity warnings for WebEngine/Cesium scene
  captures and reports the simplified Setup controls region as visually flat.
- 2026-05-24: Consolidated Terrain 3D, FPV, and Tactical scene status
  presentation behind `AnimusSceneStatus`, hiding healthy `webengine-ready`,
  `terrain-ready`, `tactical-ready`, and local fixture/provider readiness text
  from the primary UI while keeping initialization, fallback, error, stale, and
  degraded states visible. The Cesium/WebEngine status element now also hides
  known healthy ready states but remains visible for renderer fallback. Capture
  diagnostics and WebEngine/Cesium status propagation remain intact.
  Verification: `python3 tools/python/format_repo.py --check`, `cmake -S . -B
  build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-animus-qt --target animus_qt animus_qt_unit_tests
  --parallel`, `ctest --test-dir build-animus-qt --output-on-failure -R
  animus_qt`, and `python3 tools/python/capture_animus_qt_sitl.py` passed.
  Screenshot artifact: `artifacts/animus-qt-screenshots/20260524T193619Z/`.
  Compared against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`; mean pixel
  deltas for the affected screenshots were `1.23` (Terrain 3D), `4.48` (FPV),
  and `11.42` (Tactical). No new visual verification failures were found; the
  report retains existing low-diversity warnings for WebEngine/Cesium scene
  captures.
- 2026-05-24: Started the behavior-preserving foundation patch by adding shared
  QML primitives and migrating the first duplicated overlay/control surfaces.
  Verification: `cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON
  -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build-animus-qt --target
  animus_qt animus_qt_unit_tests --parallel`, `ctest --test-dir build-animus-qt
  --output-on-failure -R animus_qt`, and
  `python3 tools/python/capture_animus_qt_sitl.py` passed. Screenshot artifact:
  `artifacts/animus-qt-screenshots/20260524T182802Z/`. Compared against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`; the largest
  expected mean pixel delta was Terrain 3D workspace overlay/scene framing
  (`33.85`). No new visual verification failures were found. Healthy debug
  labels remain visible by design for this first pass.
- 2026-05-24: Moved global mock telemetry, UDP telemetry, and theme controls
  from always-visible header buttons into a compact header settings disclosure.
  The primary header now keeps link freshness and the current safe read-only
  authority visible, while chrome diagnostics verify the settings disclosure,
  theme mode, tabs, link state, and authority state. Verification:
  `cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON
  -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build-animus-qt --target
  animus_qt animus_qt_unit_tests --parallel`, `ctest --test-dir
  build-animus-qt --output-on-failure -R animus_qt`, and
  `python3 tools/python/capture_animus_qt_sitl.py` passed. Screenshot artifact:
  `artifacts/animus-qt-screenshots/20260524T185735Z/`. Compared against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`; mean pixel
  deltas were `5.4` (Map 2D), `0.8` (Terrain 3D), `5.0` (FPV), `12.6`
  (Tactical), and `6.5` (Setup). No new visual verification failures were
  found; the report retains existing low-diversity warnings for some
  WebEngine/Cesium scene captures.
- 2026-05-24: Cleaned up the Map 2D first-layer controls by replacing raw
  zoom, recenter, disclosure, warning, and pan-pad buttons with shared
  `AnimusIconButton` controls and tooltips. The provider panel now keeps the
  offline mode, provider selector, concise map/cache status, warning color, and
  scale/follow state visible while moving provider ID, map type, cache tile-set
  detail, cache DB path, and attribution into a details disclosure. Attribution
  remains visible by default as legal/source text. Verification: `python3
  tools/python/format_repo.py --check`, `cmake -S . -B build-animus-qt
  -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug`, `cmake --build
  build-animus-qt --target animus_qt animus_qt_unit_tests --parallel`, `ctest
  --test-dir build-animus-qt --output-on-failure -R animus_qt`, and `python3
  tools/python/capture_animus_qt_sitl.py` passed. Screenshot artifact:
  `artifacts/animus-qt-screenshots/20260524T192525Z/`. Compared against
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`; Map 2D mean
  pixel deltas were `4.6` (default cache) and `10.1` (seeded cache). No new
  visual verification failures were found; existing scene-diversity warnings
  remain outside this Map 2D slice.

### Remaining Decisions

- Review the cleaned-up Map 2D provider/cache disclosure in operator use,
  including attribution placement for offline/local providers.
- Approve the threshold for hiding healthy scene readiness text in Terrain 3D,
  FPV, and Tactical while preserving degraded source visibility.
- Approve the Setup section taxonomy and which raw model/cache fields remain
  visible on the first viewport.

## Baseline Reviewed

- Architecture and operator docs:
  `docs/animus_qt_architecture.md`,
  `docs/animus_operator_controls.md`, and
  `docs/animus_qgc_map_audit.md`.
- QML workspace files:
  `WorkspaceShell.qml`, `TelemetryStrip.qml`, `Map2DView.qml`,
  `Terrain3DView.qml`, `FpvView.qml`, `TacticalAttitudeView.qml`,
  `Terrain3DWebView.qml`, and `SetupView.qml`.
- Map, telemetry, vehicle model, Cesium bridge, offline cache, and capture
  workflow boundaries under `tools/animus-qt/src/**` and
  `tools/python/capture_animus_qt_sitl.py`.
- Visual baseline:
  `artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`.
- Capture report:
  `artifacts/animus-qt-screenshots/20260524T095811Z/visual-report.md`.

The visual capture passed, but the report still shows weak scene diversity in
Terrain 3D and tactical scenes. That is acceptable for current verification, but
from a product-design lens it reinforces the main issue: much of the screen is
valid but low-information chrome, debug text, or unprioritized status.

## Product Direction

Animus should feel like a focused field debugging cockpit shaped with the
restraint of a high-polish product interface, not a raw component demo. The
interface should preserve read-only safety, offline operation, and diagnostic
proof, while making the primary flight state easier to scan at a glance.

Use two review lenses at the same time:

- Product design lens: every visible control should earn its place, use familiar
  interaction patterns, and avoid exposing implementation detail as primary UI.
  The default surface should feel calm, intentional, and touchable without
  looking decorative.
- GNC/flight-engineering lens: vehicle state, link quality, navigation quality,
  flight envelope, terrain risk, mission progress, and command authority must be
  visible at the right priority. A pilot or flight-test engineer should not have
  to parse raw subsystem internals to answer "is the aircraft healthy, where is
  it going, and what should I watch next?"

The target look should be:

- Sleek: fewer boxed panels, calmer borders, less repeated button chrome.
- Focused: workspace content owns the screen; diagnostics stay reachable but
  are not always foreground.
- User-friendly: labels should describe operator intent and vehicle meaning
  rather than internal implementation where possible.
- Operator-safe: freshness, authority, position, vehicle state, and terrain
  risk remain explicit and non-color-only.
- Verification-friendly: semantic object names and capture diagnostics remain,
  but they do not have to appear as visible UI.

The key design rule is progressive status depth:

1. Primary layer: "Can I trust the link, where is the aircraft, what mode/state
   is it in, and is there immediate terrain/navigation/flight-envelope risk?"
2. Secondary layer: "Why is the status degraded, what source produced it, and
   what operator action is available?"
3. Diagnostic layer: raw packet counts, cache paths, provider IDs, GLB node
   names, WebEngine state, capture data, and implementation-level details.

Animus currently exposes too much of the third layer on the first layer. The
cleanup should not delete that information; it should put it behind the right
door.

## Current Visual Findings

### Workspace Shell

The top chrome is compact in height, but it is visually busy because every
workspace carries the same brand label, telemetry state, theme button, mock
telemetry button, UDP button, and five full-width tab buttons. In the screenshots
the header consumes only 47 pixels, yet it feels heavier than its size because
the tab strip spans the full width with bordered rectangular cells.

Issues:

- `Mock Telemetry`, `Stop`, and `UDP` are always visible even when the operator
  is focused on Map 2D, FPV, Terrain 3D, or Tactical state.
- `Light` is implementation language; it reads like a setting value instead of
  an operator mode control.
- The tab buttons repeat nearly identical QML blocks, which makes later visual
  cleanup harder to apply consistently.
- The header has no strong hierarchy between vehicle/link safety state and
  developer convenience controls.

Direction:

- Keep one global link/authority summary visible, but make it stateful and
  meaningful: `Read-only`, `SITL writable`, `Live writable`, `Link fresh`,
  `Link stale`, `No telemetry`, and similar high-level states should be more
  prominent than the transport controls.
- Move telemetry source controls, mock telemetry, theme, and capture-oriented
  utilities behind a compact settings/debug disclosure.
- Replace full-width rectangular tab cells with a calmer segmented workspace
  switcher or a left rail that keeps the active workspace obvious without
  boxing the entire top of the app.
- Avoid a header that reads like a desktop test harness. The first impression
  should be vehicle-aware, not build/debug-aware.

### Map 2D

Map 2D is functionally rich, and the overlay stack exposes useful state:
provider, offline policy, cache status, zoom, scale, follow/manual pan, warning,
directional pan, attribution, telemetry, vehicle, home, breadcrumb, mission,
fence, rally, and event overlays.

The screen reads cluttered because several of those elements compete at the
same visual level. The top-left provider panel, top-right telemetry strip,
center warning when present, right-side pan pad, and bottom attribution all use
boxed surfaces over the map. The screenshot also uses text buttons for controls
that are naturally icon controls.

GNC/operator priority:

- Highest: ownship, home, active mission leg, geofence/rally/event risk,
  navigation quality, link freshness, and terrain/map-source validity.
- Medium: zoom/scale/follow state and selected map source.
- Low: provider IDs, cache counters, cache DB path, and seeded-cache workflow.

Remove-safe candidates:

- Replace visible `^`, `v`, `<`, `>`, `o`, `x`, and expand/collapse text with
  icon controls. This preserves behavior while reducing primitive-looking UI.
- Shorten repeated cache wording such as `offline-cache / offline | empty cache`
  and `QGC-style cache | offline` in the always-visible map overlay. Full paths
  and cache DB names belong in expanded diagnostics.
- Hide the directional pan pad by default on pointer-rich desktop captures,
  while keeping it available from an accessibility/navigation control. This is
  behavior-preserving if keyboard/mouse alternatives remain.
- Collapse attribution into a low-emphasis footer or map source tooltip when
  the active provider is local/offline. Licensing text must remain available.

Consolidation candidates:

- Create a shared status pill/component for `FRESH`, `STALE`, `UNK`, warning,
  caution, and clear states.
- Create a shared map/scene overlay panel component so Map 2D, Terrain 3D, FPV,
  and Tactical do not each hand-roll rounded frames and status labels.
- Centralize small icon buttons and tooltips for snap, zoom, pan, close, and
  disclosure actions.

Keep:

- Offline/cache policy visibility, because a wrong map source has operational
  and licensing impact.
- Clear stale/unknown states; do not replace them with color-only indicators.
- Mission, fence, rally, and event overlays, even when visually simplified,
  because they are operator context rather than decoration.

### Terrain 3D

Terrain 3D has a strong full-screen scene concept, but the screenshot shows a
large blue canvas with three independent overlays: status text top-left,
telemetry top-right, clearance bottom-left, and camera mode buttons bottom-right.
The vehicle model and trail are useful, but the scene lacks enough terrain
context to justify always-visible diagnostic text.

Issues:

- `terrain fixture: cruise6dof-stanford-sim-fixture | webengine-ready` is debug
  language. It is useful during verification, but it should not be the primary
  operator label.
- Clearance is important, but the bottom-left panel uses a box-heavy table that
  visually competes with the scene.
- Camera mode buttons are text-only and sit far from the related scene status.
- The low-diversity capture warning confirms that the scene can pass
  verification while still feeling visually sparse.

Direction:

- Promote `Clearance caution`, AGL, and trend into a single risk badge with an
  optional expanded details state.
- Move terrain provider, fixture/cache path, and WebEngine readiness into a
  diagnostics drawer unless the terrain source is invalid or stale.
- Keep camera mode visible, but render it as a compact segmented control with
  clearer active state.
- Add visual hierarchy around the vehicle, route/trail, and ground reference so
  the scene is not mostly empty sky/color when terrain fixture data is sparse.
- Add flight-engineering cues before adding decorative scene chrome: altitude
  reference, recent/min clearance, climb/sink trend, track vector, and degraded
  terrain-source state are more useful than raw renderer readiness.

Keep:

- Terrain source provenance and clearance state, because offline terrain and
  altitude-reference mismatch are safety-relevant.
- WebEngine/Cesium fallback diagnostics in capture artifacts and logs.
- Camera mode controls, because camera lock/free state materially changes what
  the operator is seeing.

### FPV

FPV is intentionally minimal and the no-free-roam, fixed-FOV behavior is good.
The current view still carries full shell chrome plus top-left diagnostic text,
top-right telemetry, bottom-left terrain text, and a bottom-right `Snap` button.
The screenshot is mostly empty blue, making the overlays more prominent than
the actual view.

The FPV default should be the most cockpit-like view: sparse, stable, and
orientation-focused. Any indicator that does not help the operator interpret
attitude, flight path, clearance, camera lock, or link trust should be hidden
until degraded or requested.

Remove-safe candidates:

- Hide bottom-left `terrain-ready | local heightmap fixture` in normal
  operation. Keep it in diagnostics and show it only when terrain is degraded.
- Rename or iconize `Snap`; the current word is terse but visually generic.
- Reduce duplicated top-left status wording already represented by terrain
  diagnostics and the capture report.

Keep:

- Fixed 70 degree FOV and forward-hemisphere clamp.
- Ownship hidden in FPV to avoid near-camera clipping.
- A quick recenter/reset control, because FPV orientation drift can confuse an
  operator.

### Tactical

Tactical is the clearest candidate for a polished operator instrument. It has a
simple scene, vehicle-locked behavior, and focused attitude data. The current
presentation is still very raw: black background, separate attitude table,
repeated telemetry strip, bottom-left diagnostic text, and bottom-right `Snap`.

Issues:

- The attitude table and telemetry strip duplicate some values (`ATT`, link
  freshness) and split operator attention.
- The colored attitude rings are useful, but the canvas lacks scale, labels, or
  a refined instrument treatment.
- `tactical-ready | vehicle-locked attitude view` is debug language and should
  move out of the primary visual layer.

Direction:

- Combine attitude and link into one tactical instrument overlay that answers
  flight-state questions first: attitude, rates, heading/track, link freshness,
  and vehicle-locked camera state.
- Use a refined dark instrument surface with fewer boxes, softer grid/reference
  lines, and stronger ownship contrast.
- Keep the vehicle-locked camera state visible, but as a small lock/status
  affordance rather than a diagnostic sentence.
- Avoid adding all available values to the tactical view. It should behave like
  a focused flight-test instrument, not a telemetry dump.

Keep:

- Real GLB/profile/control-surface verification. Tactical must not silently
  fall back to the QML silhouette in accepted captures.
- Free-roam disabled for tactical mode.

### Setup

Setup exposes real value but is currently organized around implementation
surfaces: telemetry link fields, terrain vehicle model profile internals, map
policy, providers, and offline tile cache. The first viewport is dense,
form-like, and visually closer to a debug property sheet than a setup workflow.

Issues:

- `Profile ID`, `GLB asset`, node names, profile polarity, active polarity, and
  channel IDs are valid diagnostics, but they dominate the default setup page.
- Cache roots, database paths, tile counters, and default tile-set buttons are
  implementation detail for most sessions.
- The tile cache actions are mixed with policy selection instead of grouped by
  operator task.
- The controls are all always visible; there is no progressive disclosure.

Direction:

- Reorganize Setup by task:
  `Readiness`, `Telemetry Link`, `Vehicle Model`, `Maps And Terrain`,
  `Logs`, and `Diagnostics`.
- Default each task to a summary row with status, last update, and a primary
  action. Put raw IDs, paths, cache DB names, GLB asset paths, and per-surface
  polarity internals behind details.
- Keep destructive or network-affecting actions explicit, grouped, and guarded.
- Make `Vehicle Model` about what the operator needs to verify first: selected
  model, loaded asset health, surface mapping health, and any reversed surfaces.
- Add readiness summaries that read like flight engineering gates: link,
  navigation, estimator, battery/power when available, mission, terrain, model,
  logging, and write authority. Show raw reasons only after expansion.

Keep:

- Read-only posture by default.
- Raw implementation detail in an expanded diagnostics path, because it is
  necessary for debugging model profiles, cache state, and eventual hardware
  integration.
- Map policy controls, because online/offline behavior has operational and
  licensing implications.

## Remove-Safe Inventory

These items can be removed or hidden from the always-visible UI without deleting
underlying behavior:

- Always-visible debug status sentences such as `webengine-ready`,
  `terrain-ready`, `local heightmap fixture`, and cache DB paths when the state
  is healthy.
- Text glyph buttons for primitive icon actions: `^`, `v`, `<`, `>`, `o`, `x`,
  and one-letter-ish disclosure controls.
- Repeated visible provider/cache wording where a concise status plus details
  disclosure would preserve the information.
- Repeated top-left scene status labels in Terrain 3D, FPV, and Tactical.
- Per-surface model internals on Setup's first viewport.
- Always-visible mock/UDP/theme controls in the global shell.

Do not remove the data paths, capture diagnostics, or semantic object names
behind these items. The cleanup target is visual priority, not loss of
observability.

## Consolidation Inventory

The QML already shows several repeated patterns that should be centralized
before a broad visual refresh:

- Workspace tab/button styling in `WorkspaceShell.qml`.
- Overlay frames used by Map 2D, Terrain 3D, FPV, Tactical, and
  `TelemetryStrip.qml`.
- Status text and color logic for `FRESH`, `STALE`, `UNK`, `clear`, `caution`,
  and `warning`.
- WebEngine readiness, fallback, capture, camera inspection, and
  control-surface diagnostic plumbing across Terrain 3D, FPV, Tactical, and
  `Terrain3DWebView.qml`.
- Icon buttons and compact controls for snap, zoom, pan, disclosure, close,
  camera modes, and reset actions.
- Setup detail rows, summary rows, and expandable diagnostics sections.

Suggested shared QML components:

- `AnimusStatusBadge`
- `AnimusOverlayPanel`
- `AnimusIconButton`
- `AnimusSegmentedControl`
- `AnimusTelemetrySummary`
- `AnimusDiagnosticsDrawer`
- `AnimusSetupSection`
- `AnimusSceneStatus`

These components should stay in Altair's Qt shell. Do not push them into Bayek.

## Decluttered Workflow Proposal

Default state:

- Show workspace switcher, selected link/authority state, and the active
  workspace.
- Show only safety-relevant overlays by default: link freshness, vehicle
  position/attitude essentials, terrain/clearance risk, map source risk, and
  explicit unknown states.
- Keep scene/view controls close to the scene and use compact icon or segmented
  controls.
- Let normal states be quiet. Reserve heavy visual treatment for stale, unknown,
  caution, warning, or operator-actionable states.

Diagnostics state:

- One global diagnostics drawer exposes telemetry source controls, mock
  telemetry, UDP endpoint state, theme, capture status, WebEngine/Cesium
  status, cache DB paths, provider details, and model/profile internals.
- Each workspace can expose local details, but the visual language should be
  consistent and collapsible.

Setup state:

- Setup becomes a task dashboard rather than a property sheet.
- Network, delete, import/export, erase, and future write-capable actions stay
  explicit, separated, and guarded.

Capture state:

- Capture workflows should continue recording semantic diagnostics even if the
  corresponding debug labels are hidden in normal UI.
- Future visual checks should compare before/after screenshots and flag both
  regressions and intentional tradeoffs.

## Aggressive Visual Redesign Direction

This is a larger design target, not a requirement for the next cleanup patch:

- Use a restrained cockpit palette with neutral surfaces, strong safety colors,
  and a separate accent for selection. Avoid a one-note blue/gray app shell.
- Reduce borders by using elevation, spacing, and typography hierarchy rather
  than every section being a framed rectangle.
- Treat the map and 3D/camera surfaces as full-bleed primary content. Overlays
  should float lightly and occupy less permanent area.
- Make telemetry scannable with grouped numbers and badges instead of compact
  tables everywhere.
- Replace text buttons with recognizable icons where the action is standard,
  with tooltips for less obvious controls.
- Make Setup denser but calmer: grouped rows, stable columns, details
  disclosure, and fewer nested boxes.
- Use warning/caution/unknown language only where it changes operator
  interpretation. Healthy debug readiness should be quiet.
- Prefer plain, precise aviation language over software plumbing language:
  `Terrain source degraded`, `Link stale`, `No GPS fix`, `Read-only`, and
  `Vehicle locked` are better first-layer labels than `webengine-ready`,
  `local heightmap fixture`, `offline-cache`, or raw endpoint/counter strings.
- Use one glanceable status hierarchy rather than many equal boxes. A polished
  interface should make the most important state feel inevitable, not louder.

## Flight Status Hierarchy

The UI should sort available information by operational usefulness:

- Tier 1, always visible when relevant: link freshness, command authority,
  ownship position, attitude, altitude reference, groundspeed/vertical speed,
  GPS/navigation quality, active mission/route cue, terrain or geofence risk,
  and explicit unknown states.
- Tier 2, visible on workspace demand or degraded state: packet age, update
  rates, source identity, map/terrain source, cache availability, model/profile
  health, and camera lock/mode.
- Tier 3, diagnostic drawer only: raw endpoint, datagram counters, decode error
  counts, cache DB paths, tile-set IDs, GLB node names, polarity internals,
  WebEngine/Cesium readiness, capture paths, and fixture names.

This hierarchy prevents the common failure mode where adding helpful indicators
slowly makes the screen less helpful. New indicators should be admitted to the
default surface only if they change immediate flight interpretation or reduce a
real operator decision burden.

## Keep Because It Is Justified

The following complexity is justified and should survive cleanup:

- Read-only default posture and explicit command authority states.
- Link freshness and unknown/stale semantics.
- Offline map policy, attribution, provider licensing boundaries, and cache
  provenance.
- Terrain source, terrain-clearance state, and altitude-reference diagnostics.
- WebEngine/Cesium fallback and capture diagnostics.
- GLB profile, model, and control-surface verification paths.
- Mock telemetry and deterministic capture controls, although they should move
  out of the always-visible operator chrome.
- Accessibility alternatives for pan/zoom/recenter actions.
- Raw paths, IDs, counters, and cache details in diagnostics for field debugging
  and eventual hardware use.

## Recommended Implementation Sequence

1. Add shared QML primitives for status badges, overlay panels, icon buttons,
   segmented controls, and setup sections.
2. Move global debug controls into a diagnostics/settings disclosure while
   preserving all current actions.
3. Convert Map 2D primitive text buttons and duplicated cache/status text to
   compact controls with details disclosure.
4. Consolidate Terrain 3D, FPV, and Tactical scene status handling so healthy
   WebEngine/fixture text is hidden but degraded states remain explicit.
5. Rework Setup into task sections with diagnostic details collapsed by default.
6. Refresh visual styling after behavior-preserving consolidation, then run full
   Animus Qt visual verification and compare before/after screenshots.

## Verification For Future Cleanup

For this documentation-only audit, `python3 tools/python/format_repo.py --check`
is sufficient.

Any future code cleanup touching Animus UI must run:

```sh
cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-animus-qt --target animus_qt animus_qt_unit_tests --parallel
ctest --test-dir build-animus-qt --output-on-failure -R animus_qt
python3 tools/python/capture_animus_qt_sitl.py
```

Each UI cleanup should compare the new capture against
`artifacts/animus-qt-screenshots/20260524T095811Z/screenshots/`, note visual
regressions or intentional tradeoffs, and keep the capture diagnostics passing.

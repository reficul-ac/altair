# Animus Operator UI And Vehicle Model Phase

This plan captures the next product step for Animus: move from a terrain and
telemetry renderer toward a polished native desktop telemetry visualization
application. The implementation must remain native C++ and OpenGL. It must not
introduce React, Electron, Tauri, browser views, CesiumJS, or a terrain engine
rewrite.

## Current Repository Baseline

Animus is currently implemented under `animus/` as a future standalone project
boundary. The native app exists at `animus/apps/animus` and uses GLFW, GLEW,
OpenGL, and Dear ImGui. The core modules found during inspection are:

- `render_core`: GLFW window, OpenGL setup, shader, texture, mesh, render stats,
  and ImGui integration.
- `terrain_core`: terrain tile contracts, cache, streams, tile sources, datum
  handling, terrain mesh data, and height sampling.
- `geo_core`: tile math and Web Mercator coordinate helpers.
- `telemetry_core`: telemetry samples, entities, tracks, events, playback clock,
  MAVLink reduction, and log import paths.
- `telemetry_live`: UDP MAVLink receive, live buffer, and trail decimation.
- `apps/animus`: CLI, app orchestration, camera, terrain drawing, telemetry
  overlay drawing, capture/export, and ImGui workspace panels.

The current UI is an app-owned ImGui shell with a status bar, navigation modes,
telemetry controls, capture controls, a developer diagnostics panel, an
inspector, and playback timeline. It is native and useful, but it still reads
more like a developer console than the final operator product. No existing
GLB/glTF loader, vehicle asset registry, YAML descriptor parser, dedicated
vehicle model module, icon system, or production widget framework was found.

## Product Direction

The target experience is a fast operator application centered on one large 3D
terrain viewport. UI chrome should be calm, minimal, and progressively
disclosed: a compact top status bar, left-side navigation/entity list, right
inspector, bottom live/timeline strip, and clean viewport labels. The design
inspiration is Apple-like in discipline only: clear hierarchy, spacing,
restrained color, obvious selection, strong defaults, smooth navigation, and
little unnecessary chrome. Animus must not copy Apple branding, fonts, assets,
or proprietary UI elements.

The user should be able to see live telemetry entities, identify them, select a
vehicle, inspect its telemetry, follow it with the camera, understand whether
telemetry is live/stale/degraded/invalid, assign a vehicle type/model, and see a
recognizable model when available. If a model cannot load, telemetry display
must continue with an icon fallback.

## Operator UI And Debug UI

Operator UI is always available and should show only the information needed for
navigation and decision-making: telemetry source state, entity list, selected
vehicle, useful telemetry values, live/replay state, layer state, and concise
health indicators.

Developer UI is optional and hidden by default. It can remain Dear ImGui-heavy
and should own dense implementation data such as tile state tables, cache
counters, GPU upload budgets, parser diagnostics, worker queues, model node
debug, transform debug, asset errors, and frame timing.

The operator UI may use the existing ImGui layer for the near term because that
is the native UI system currently present. The implementation should style it as
an app shell, not raw debug panels, and keep diagnostic detail in Developer mode.
A future retained/native UI layer can be considered only after the workflows and
state model are proven.

## Target UI Components

- Top status bar: app name, live/replay/record state, terrain state, layer
  summary, and connection health.
- Left sidebar: Entities, Layers, Sessions later, with entity search/filter
  planned.
- Entity list: status dot, vehicle type/icon fallback, name or `system:component`
  ID, last update age, altitude, speed, stale/degraded warning, click selection,
  and double-click focus later.
- Right inspector: empty state when nothing is selected; selected entity ID,
  vehicle type/model, live state, latitude/longitude, altitude, altitude
  reference, terrain-relative warnings, speed, heading, pitch, roll, last update
  time, track visibility, label visibility, follow control, and model controls.
- Bottom status/timeline strip: live clock, telemetry update rate, selected
  entity time, status messages, and space reserved for replay timeline.
- Viewport overlays: model or icon, compact label, heading vector, optional
  altitude stem, track tail, selected ring/outline, and stale warning.

Recommended interactions:

- Single-click entity selects it.
- Double-click focuses camera on it later.
- `F` toggles follow-selected.
- `Escape` clears selection or exits focus/follow before closing once the app
  input model supports that distinction.
- `Space` toggles replay play/pause later.
- Command palette and entity search/filter can follow once core workflows exist.

## Vehicle Architecture

Add a later `vehicle_core` module rather than scattering vehicle model policy
through the app or renderer. It should depend on core data contracts and use
`render_core` for GPU upload/drawing boundaries. It should not make
`render_core` own telemetry semantics, and it should not make `telemetry_core`
own rendering.

Planned responsibilities:

- `VehicleDefinition`: parsed descriptor data, model paths, type, orientation
  correction, dimensions, nodes, LODs, rendering defaults, and telemetry mapping.
- `VehicleModel`: loaded GLB data, mesh/material/texture references, node
  hierarchy, bounds, and named node lookup.
- `VehicleRegistry`: package discovery under vehicle asset roots, validation,
  default vehicle mapping by type, and fallback definitions.
- `VehicleInstance`: binds telemetry entity ID to vehicle definition/model,
  current pose, selected/live/stale/degraded state, and computed transforms.
- `VehicleAnimator`: telemetry-driven propeller, gimbal, and control surface
  animation.
- `VehicleRenderer`: icon/model/hybrid display mode selection, labels,
  overlays, selected state, and safe model-failure fallback.

GLB/glTF loading should use an existing project loader if one is added before
this phase. If none exists, prefer `cgltf` or `tinygltf` over Assimp for the
runtime path because Animus only needs GLB/glTF 2.0 as its native delivery
format. GPU resources must be uploaded through `render_core`, not ad hoc OpenGL
spread through vehicle logic.

## Phased Plan

Phase 0: Repo inspection and docs.

- Document current modules, target UI, vehicle asset format, assumptions, and
  open questions.
- Acceptance: this document and `docs/vehicle_asset_spec.md` exist, and
  architecture docs include the added phases.

Phase 1: Entity UI foundation.

- Improve app-local selection state, entity list, selected inspector, live/stale
  visual states, bottom status strip, and follow-selected action if camera
  support allows it.
- Acceptance: entity selection is obvious in UI and viewport, inspector shows
  useful telemetry, stale/degraded state is visible, and no GLB is required.

Phase 2: Viewport overlays.

- Render readable fallback icons/labels for telemetry entities, selected ring,
  heading vector, and track tail when track data exists.
- Acceptance: telemetry entities remain readable without models and selected
  state is clear.

Phase 3: Vehicle descriptor and registry.

- Add descriptor parsing, validation, example vehicle packages, default mapping
  by entity/vehicle type, and readable asset errors.
- Acceptance: vehicle packages can be discovered and assigned, and failures
  fall back to icon mode.

Phase 4: GLB model loading.

- Add a GLB loader path preserving meshes, materials, textures, node hierarchy,
  node names, and bounds. Render one static model through `render_core`.
- Acceptance: one GLB renders at plausible scale/orientation, and failure falls
  back to icon mode.

Phase 5: Telemetry-bound vehicle rendering.

- Attach models to live/playback telemetry entities, apply lat/lon/altitude,
  yaw/pitch/roll, correction transform, origin offset, scale, selected state,
  and icon/model/hybrid display modes.
- Acceptance: a quadcopter or plane model moves over terrain with telemetry and
  missing models never break entity display.

Phase 6: Vehicle animation.

- Animate propellers first, then gimbals/control surfaces as descriptor node
  mappings and telemetry fields become available.
- Acceptance: missing animation nodes are non-fatal and live animation does not
  require RPM telemetry at first.

Phase 7: UI polish.

- Refine spacing, typography, colors, keyboard navigation, empty states, error
  copy, and operator/debug separation.
- Acceptance: the viewport dominates, the selected-entity workflow feels
  polished, and debug panels remain hidden unless requested.

Phase 8: Asset validation tool.

- Add `animus/tools/validate_vehicle_asset.py` or equivalent to validate
  descriptors, required paths, orientation fields, LOD references, and node names
  where GLB parsing is available.
- Acceptance: invalid vehicle packages produce readable errors and docs explain
  how to add a new asset.

## Initial Implementation Slice

The safest useful slice is Phase 1 plus part of Phase 2:

- Keep the existing terrain renderer.
- Keep the existing ImGui/OpenGL native architecture.
- Improve the operator-facing entity list, inspector, bottom strip, selected
  state, and fallback viewport markers.
- Add follow-selected only as app-local camera behavior.
- Do not add GLB loading, YAML parsing, new runtime dependencies, or large binary
  assets in this slice.

## Tests And Validation

Use checks proportional to touched files:

- Build Animus and run CTest with `python3 animus/tools/verify_animus.py`.
- For UI/overlay behavior, run a native smoke capture, preferably under Xvfb on
  headless systems:

```bash
xvfb-run -a animus/build/apps/animus/animus --smoke --frames 120 \
  --capture-png /tmp/animus_operator_ui.png
```

Manual validation should verify that the entity list is readable, selected state
is obvious in the list/inspector/viewport, stale telemetry is visible, fallback
icons render when no model exists, and Developer diagnostics stay hidden unless
selected.

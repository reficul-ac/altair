# Animus Module Contracts

Phase B defines contracts before rendering code exists. Public headers live
under `animus/libs/<module>/include/animus/<module>/` and use
`animus::<module>` namespaces.

## Dependency Direction

- `geo_core` owns vehicle-agnostic geographic types and math. It must not
  depend on rendering, terrain loading, telemetry, Altair, or Bayek.
- `terrain_core` owns terrain-facing models such as layers, rasters, tile
  state, cache identity inputs, and later mesh/tile scheduling interfaces. It
  may depend on `geo_core` after Phase C.
- `data_core` owns generic cache/path helpers and later compression/archive or
  SQLite/MBTiles utilities. It may consume terrain model contracts when forming
  cache keys.
- `render_core` owns native rendering primitives and future OpenGL resource
  boundaries. It must not own tile loading policy or telemetry models.
- `telemetry_core` is delayed. When added, it may query terrain height through
  narrow interfaces but must not drive terrain cache or renderer ownership.
- `vehicle_core` owns vehicle descriptors, registry validation, and CPU-side
  static GLB loading. It may parse reusable asset packages but must not own
  OpenGL resources, telemetry selection policy, app UI, or Altair-specific
  vehicle assignments.
- `apps/animus` owns UI, CLI parsing, runtime state, capture/export commands,
  and developer panels. App modules may compose the core libraries, but
  `terrain_core`, `telemetry_core`, `render_core`, `vehicle_core`, and
  `data_core` must remain independent of app UI and command-line policy.

## Public Header Policy

Headers should expose small data contracts before implementation-heavy classes.
Use snake_case for C++ data members and lower-case stable strings for serialized
or logged contract names. Do not expose Altair or Bayek headers from Animus
public interfaces.

## Current Phase B Contracts

`terrain_core` defines:

- `LayerType`
- `LayerSpec`
- `RasterFormat`
- `SamplingMode`
- `Raster`
- `TileState`

`data_core` defines layer-level cache prefix helpers. Tile-coordinate cache keys
remain Phase C work, after `geo_core::TileCoord` exists.

`apps/animus` keeps `main.cpp` as the exception-handling entrypoint. CLI options
live in `options.cpp`, framebuffer capture helpers live in `capture.cpp`,
terrain/render/live orchestration stays in `animus_app.cpp`, and app-owned
workspace state plus Dear ImGui panels live in `ui.cpp`/`ui.hpp`. Core libraries
must not include app UI headers.

`vehicle_core` defines:

- `VehicleType`
- `VehicleDefinition`
- `VehicleOrientation`
- `VehicleDimensions`
- `VehicleRegistry`
- `VehicleRegistryDiagnostic`

Vehicle packages are loaded from `animus/assets/vehicles/`. The current default
definition is `animus.rc_plane.generic`; all telemetry entities use that default
until a future assignment system exists.

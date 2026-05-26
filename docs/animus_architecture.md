# Animus Architecture

This document defines the target architecture for Animus, a future native Linux
desktop application for high-performance 3D terrain and telemetry
visualization. Animus should be built incrementally. The first deliverable is a
terrain-only native testbed, not the full telemetry application.

The eventual Animus codebase should live under a single top-level `animus/`
folder so it can later be shipped as its own repository. Even if early
prototypes live beside Altair, the project boundary should remain clear:
Animus rendering, terrain, data, and telemetry modules belong under `animus/`
and should not be scattered across Altair or Bayek.

## 1. Animus Vision

Animus is a native Linux desktop app for high-performance 3D terrain and
telemetry visualization.

The long-term product vision includes:

- streamed 3D terrain
- imagery, elevation, and bathymetry layers
- local and remote tiled terrain sources
- local cache and offline-friendly workflows
- shader-based hill shading
- seamless LOD transitions
- terrain-draped overlays
- later telemetry playback, moving entities, tracks, sensors, events, and
  timeline control

The immediate product vision is narrower:

- build a smooth native terrain renderer first
- do not add telemetry until terrain is stable
- avoid a web UI entirely
- avoid trying to build the whole app in one pass

Animus must not use a browser frontend. It should not use CesiumJS, React,
Electron, Tauri, or webview-based architecture.

## 2. Core Engineering Principle

Animus terrain should maintain a stable visible surface at all times. It should
improve that surface opportunistically as better tiles arrive. The renderer
should never block waiting on I/O, decoding, mesh building, or network fetches.
It should never remove old terrain until replacement terrain is ready on the
GPU.

Bad:

- request child tiles
- remove parent immediately
- child tiles are not ready
- holes or blank terrain appear

Good:

- keep rendering parent tile
- request child tiles in background
- decode child tiles
- build child CPU meshes
- upload child GPU resources
- only then replace parent with children

This rule is the central smoothness requirement for the terrain engine.

## 3. Recommended Technology Stack

The initial terrain-first stack should stay small and focused:

- C++20 or C++23
- CMake
- Conan 2
- Ninja
- Python 3 tooling
- GLFW for native Linux window/context creation
- GLEW for OpenGL loading
- OpenGL Core Profile, minimum 3.2, preferably 4.x where available
- nixGL support for Linux/Nix GPU linking/runtime cases
- GDAL for GeoTIFF, raster, and geospatial input
- libpng for PNG imagery and Terrain-RGB-style decoding
- Boost for utilities and optional worker/thread pool support
- zlib, zstd, and lz4 for compressed cache/data formats
- SQLite for MBTiles/cache metadata later
- GoogleTest for unit tests
- optional Dear ImGui or a simple internal debug overlay for terrain debugging
- optional RenderDoc and Tracy as developer tooling, not hard runtime
  dependencies

Delayed or later stack items:

- MCAP for timestamped telemetry logs
- Protobuf for schemas/messages
- HDF5 + HighFive for scientific and large structured data
- FFmpeg and libaom-av1 for video/export workflows
- yaml-cpp, tinyxml2, libkml, libarchive, and libcurl for config, geospatial
  imports, archives, and remote sources
- Sol2 for scripting only after the core is stable

These later dependencies should not be pulled into the first terrain milestone
unless a specific terrain requirement needs them.

## 4. Repository/Module Structure

Animus should be structured as a single self-contained folder that can become
the root of a standalone repository:

```text
animus/
  CMakeLists.txt
  conanfile.py
  cmake/
  docs/
    animus_architecture.md
    terrain_pipeline.md
    telemetry_pipeline.md
  apps/
    animus/
      CMakeLists.txt
      src/
        main.cpp
        AnimusApp.cpp
        AnimusApp.hpp
  libs/
    render_core/
      include/
      src/
    geo_core/
      include/
      src/
    terrain_core/
      include/
      src/
    telemetry_core/
      include/
      src/
    data_core/
      include/
      src/
  tools/
    prepare_terrain_pack.py
    inspect_tile.py
    validate_tile_pyramid.py
    merge_elevation_bathymetry.py
  tests/
    geo_core_tests/
    terrain_core_tests/
    render_core_tests/
    telemetry_core_tests/
  data/
    sample_area/
      imagery/
      elevation/
      bathymetry/
  cache/
    .gitkeep
```

`apps/animus` is the native desktop application and terrain regression harness.
It is terrain-first for now: no telemetry or timeline until the terrain runtime
is stable. It depends on `terrain_core`, `render_core`, `geo_core`,
`data_core`, and later `telemetry_core`.

`libs/render_core` owns GLFW window wrapping, OpenGL context/debug setup, GLEW
initialization, shader wrappers, texture wrappers, mesh/VBO/IBO/VAO wrappers,
camera support, a render device abstraction, GPU upload queue, and render
statistics.

`libs/geo_core` owns Slippy Map/Web Mercator math, `TileCoord`, `GeoBounds`,
tile-to-bounds, lat/lon-to-tile, tile UV, parent/child tile utilities, ENU
frame utilities, and later vertical datum abstractions.

`libs/terrain_core` owns `LayerSpec`, `Raster`, `TerrainTile`, `TileState`,
tile cache, tile source interfaces, raster decoders, elevation/bathymetry
merge, mesh builder, terrain scheduler, tile quadtree, terrain renderer,
fallback/synthesis logic, and terrain height queries.

`libs/data_core` owns generic file/cache utilities, compression helpers,
archive helpers later, local project/cache paths, and SQLite/MBTiles
integration later.

`libs/telemetry_core` is intentionally delayed. It will later own telemetry
sample schemas, entity state, track playback, MCAP/Protobuf/HDF5 ingestion,
timeline interpolation, and terrain-relative placement.

## 5. Terrain-First Development Strategy

Animus should be developed in this order:

Stage 1: native terrain runtime

- build a standalone native terrain testbed
- no telemetry
- prove local tile loading, elevation decoding, mesh generation, shading, async
  loading, caching, and LOD

Stage 2: `terrain_core`

- extract reusable terrain systems from the native runtime into a library
- preserve `apps/animus` as the test/demo harness

Stage 3: minimal Animus app shell

- app shell hosts the terrain renderer and basic layer controls/debug panels

Stage 4: `telemetry_core`

- add telemetry import/playback after terrain is stable
- support MCAP, Protobuf, HDF5, and CSV/JSON debug inputs
- add moving entities, tracks, timeline, events, and inspection

Stage 5: advanced terrain/data features

- bathymetry/elevation merge
- GeoTIFF overlays
- remote tile sources
- tile synthesis
- cache prewarming
- geoid/datum correction
- video/export workflows

## 6. Terrain Tile Model

Tile identity should be independent of rendering:

```cpp
struct TileCoord {
    int z;
    int x;
    int y;
};
```

Rules:

- `z` is zoom level
- `x` and `y` are XYZ/Slippy Map tile indices
- at zoom `z`, the grid is `2^z` by `2^z`
- use Web Mercator XYZ tile addressing
- this model should not depend on OpenGL or terrain rendering classes

Required functions:

```text
latLonToTile(latDeg, lonDeg, z) -> TileCoord
tileToBounds(TileCoord) -> GeoBounds
latLonToTileUv(latDeg, lonDeg, TileCoord) -> Vec2
parent(TileCoord) -> TileCoord
children(TileCoord) -> array of 4 TileCoord
tileKey(TileCoord) -> stable string/hash
```

## 7. Layer Model

A tile coordinate describes where a tile is. A layer spec describes what data
should be loaded for that tile.

```cpp
enum class LayerType {
    Imagery,
    Elevation,
    Bathymetry,
    Overlay
};

struct LayerSpec {
    LayerType type;
    std::string source;
    std::string style;
    std::string extra;
    int resolution = 256;
    int minZoom = 0;
    int maxZoom = 18;
};
```

One logical terrain tile can carry multiple aligned rasters:

- imagery raster
- elevation raster
- bathymetry raster
- merged height raster

## 8. Raster Model

The internal raster model should normalize source formats before the rest of
the pipeline consumes them:

```cpp
enum class RasterFormat {
    Float32,
    UInt8RGB,
    UInt8RGBA
};

enum class SamplingMode {
    Center,
    Corner
};

struct Raster {
    int width = 0;
    int height = 0;
    int channels = 0;
    RasterFormat format;
    SamplingMode samplingMode;
    std::vector<float> floatData;
    std::vector<uint8_t> byteData;
    std::optional<float> noDataValue;
};
```

Rules:

- elevation and bathymetry should be normalized to single-channel float32
  meters
- imagery should be normalized to RGB/RGBA byte textures
- the renderer should not care whether data originally came from GeoTIFF,
  MBTiles, PNG, Terrain-RGB, or a generated cache tile
- GDAL/libpng decode into `Raster`; the rest of the pipeline consumes `Raster`

Sampling and stitching rules:

- even-sized height rasters may be treated as center-sampled
- center-sampled rasters may need padding with an extra row/column for
  stitching
- odd-sized or explicitly corner-sampled rasters can be used directly
- edge continuity must be handled from the beginning

## 9. Terrain Tile State Machine

Every tile must have an explicit state machine:

```cpp
enum class TileState {
    Missing,
    Queued,
    Loading,
    Decoding,
    Decoded,
    BuildingMesh,
    ReadyCpu,
    UploadQueued,
    ReadyGpu,
    Visible,
    Failed,
    UsingFallback,
    Retiring
};
```

Avoid vague booleans such as `isLoaded`, `isLoading`, or `hasMesh`. Explicit
states are critical for debugging smooth streaming. `apps/animus` must show a
tile-state debug view, each state should have a debug color, and tile state
transitions should be logged or inspectable.

Suggested debug colors:

- Missing: dark gray
- Queued: blue
- Loading: cyan
- Decoding: yellow
- BuildingMesh: orange
- ReadyCpu: light green
- UploadQueued: purple
- ReadyGpu: green
- Visible: normal rendering
- Failed: red
- UsingFallback: magenta
- Retiring: dark green

## 10. Tile Loading Pipeline

Render thread responsibilities:

- read camera state
- compute desired tile wishlist
- compare desired tiles against resident tiles
- enqueue missing work
- upload a limited number of ready CPU tiles to GPU each frame
- draw ready/visible terrain
- keep fallback/parent tiles visible until replacements are ready

Worker thread responsibilities:

- disk/cache lookup
- remote fetch later
- GDAL/libpng decode
- Terrain-RGB decode
- raster normalization
- elevation/bathymetry merge
- raster padding/stitch preparation
- CPU mesh generation
- CPU-side bounds/statistics
- generated tile writes

Hard rule: worker threads must never create OpenGL objects.

Workers output prepared CPU-side tiles:

```cpp
struct PreparedTile {
    TileCoord coord;
    Raster imagery;
    Raster height;
    CpuMesh mesh;
};
```

The render thread converts `PreparedTile` into:

- OpenGL texture objects
- VBO/IBO/VAO mesh objects
- resident `TerrainTile` GPU resources

## 11. GPU Upload Budget

Even if many tiles finish loading at once, Animus must limit GPU uploads per
frame.

Suggested configurable budgets:

- `maxTextureUploadsPerFrame = 2` to `4` initially
- `maxMeshUploadsPerFrame = 2` to `4` initially
- `maxUploadBytesPerFrame` configurable

Smoothness depends on avoiding large frame spikes. Tile streaming should be
progressive and predictable.

## 12. Terrain Rendering Model

Each renderable terrain tile should have:

- imagery texture
- height texture
- terrain mesh
- optional normal data
- tile bounds
- state
- cache metadata

The mesh should be a regular subdivided grid. Vertex positions are generated
from tile-local UV coordinates, with height sampled from the merged height
raster. Optional skirt geometry on edges hides cracks. Attributes should include
position, UV, and normal/elevation where useful.

The height texture should be a single-channel float texture such as `GL_R32F`,
with linear filtering and clamp-to-edge wrapping. Fragment shaders should use it
for analytic hill-shading normals.

Shader concept:

- sample height at the current UV
- sample neighboring heights
- compute a slope-derived normal
- combine imagery color with hill shading
- later support sun direction, ambient, contour, and debug modes

## 13. Seam and Crack Handling

Seams must be addressed early, not later.

Initial recommended method:

- use terrain skirts on tile edges
- optionally combine with padded border samples

Later possible methods:

- shared edge samples
- neighbor-aware LOD stitching
- crack-fix index buffers
- geomorphing
- parent/child crossfade

First target:

- 3x3 same-LOD tiles render with no visible cracks
- parent-child LOD transitions do not expose holes

## 14. No-Holes LOD Policy

Animus must render the best available coverage for the camera view, not simply
the ideal desired tile set.

When high-resolution child tiles are not ready:

- keep rendering the lower-resolution parent
- request children in background
- wait until all required children are `ReadyGpu`
- then replace parent with children

Do not remove a parent tile just because children were requested. This is the
main smoothness rule.

## 15. Adaptive Tile Wishlist

The tile scheduler should:

- use camera/view state
- choose visible tile coverage
- choose the highest zoom level that fits a tile budget
- prioritize center-screen/near-camera tiles
- allow a temporary overage factor `K` to reduce holes
- cap total resident tiles
- cap outstanding jobs

Pseudo-behavior:

```text
chooseZoom(camera, minZoom, maxZoom, tileBudget):
    for z from maxZoom down to minZoom:
        visibleTiles = computeVisibleTiles(camera, z)
        if visibleTiles.count <= tileBudget:
            return z, visibleTiles
    return minZoom, computeVisibleTiles(camera, minZoom)
```

Early versions may use a simpler fixed tile patch before full camera-driven
wishlist selection is implemented.

## 16. Cache Hierarchy

Cache hierarchy:

- L0: visible/resident GPU tiles
- L1: CPU raster cache
- L2: decoded tile cache
- L3: disk tile cache
- L4: local source files, such as GeoTIFF, MBTiles, XYZ folders
- L5: remote providers later

Cache key:

- `LayerSpec` identity + `TileCoord`

Examples:

```text
imagery/source/style/z/x/y
elevation/source/style/z/x/y
bathymetry/source/style/z/x/y
```

Runtime should first support a simple local XYZ folder cache:

```text
cache/
  imagery/
    z/
      x/
        y.png
  elevation/
    z/
      x/
        y.f32 or y.png or y.tif
  bathymetry/
    z/
      x/
        y.f32 or y.tif
```

Later:

- MBTiles
- GeoTIFF tile extraction
- generated/synthesized tiles
- remote HTTP tile providers
- cache metadata in SQLite

## 17. Tile Source Interfaces

Conceptual source and decoder interfaces:

```cpp
class TileSource {
public:
    virtual bool hasTile(const TileCoord&, const LayerSpec&) = 0;
    virtual TilePayload loadTile(const TileCoord&, const LayerSpec&) = 0;
};

class RasterDecoder {
public:
    virtual Raster decode(const TilePayload&, const LayerSpec&) = 0;
};
```

Initial source types:

- `XyzFolderTileSource`
- `PngImageryDecoder`
- `TerrainRgbDecoder`
- `Float32HeightTileDecoder`

Later source types:

- `GdalGeoTiffTileSource`
- `MbtilesTileSource`
- `RemoteHttpTileSource`
- `GeneratedTileSource`

## 18. Terrain-RGB and Elevation Decoding

One early supported height source can be Terrain-RGB-style PNG.

Runtime flow:

- load PNG
- decode RGB pixels
- convert to float meters
- store as `Raster` `Float32`
- build mesh and height texture from it

GDAL-backed GeoTIFF support should be added after the simple local tile path
works.

## 19. Elevation and Bathymetry Merge

Elevation/bathymetry merge is a later `terrain_core` feature, not part of the
first tile milestone.

Goal:

- load elevation raster
- load bathymetry raster
- merge into one master height raster

Initial merge rule:

- land height from elevation
- underwater height from bathymetry
- hard crossover at sea level

The renderer should only consume the merged height raster.

## 20. GeoTIFF Overlays

GeoTIFF overlays are a later feature.

Goal:

- user-added GeoTIFF imagery overlays
- overlay has geographic bounds, opacity, draw order, and source metadata
- overlay is draped onto terrain, not drawn as a flat screen-space image
- overlay samples or conforms to terrain height

Do not put this in the first milestone unless terrain is already stable.

## 21. Coordinate Systems and Vertical Datum

Needed concepts:

- Web Mercator tile addressing for tile selection
- local terrain patch coordinates for rendering
- ENU frame for local high-precision placement
- height values in meters
- later ellipsoid height vs MSL/orthometric height
- later EGM/geoid correction support

Do not mix telemetry altitude and terrain height without metadata once
telemetry is introduced.

## 22. Terrain Height Queries

Future terrain query API:

```text
heightAt(lat, lon) -> optional meters
tileAt(lat, lon, zoom) -> TileCoord
worldPosition(lat, lon, altitude, datum) -> Vec3
terrainRelativePosition(lat, lon, offsetMeters) -> Vec3
```

These APIs are needed later for telemetry placement.

## 23. Telemetry Architecture, Delayed

Telemetry is intentionally delayed until terrain is stable.

`telemetry_core` should start with standalone offline MAVLink `.tlog` playback.
It must stay vehicle-agnostic and must not depend on Altair, Bayek, Python
tools, rendering, UI, or live network ingest. CSV/JSON are debug/export paths,
not the primary Phase K input. Live UDP, MCAP, Protobuf, and HDF5 are deferred
until deterministic playback is solid.

`telemetry_core` should support:

- `TelemetrySample`
- `Entity`
- `Track`
- `Event`
- `Timeline`
- `PlaybackClock`
- MAVLink v1 and unsigned MAVLink v2 frame parsing
- common MAVLink messages for playback: `HEARTBEAT`, `GLOBAL_POSITION_INT`,
  `GPS_RAW_INT`, `ATTITUDE`, and `VFR_HUD`
- unsupported valid MAVLink messages preserved as events
- parser diagnostics for CRC failures, truncated frames, signed MAVLink v2
  frames, unsupported versions, and malformed input
- interpolation
- terrain-relative placement
- optional CSV/JSON debug import/export

Example conceptual sample:

```cpp
struct TelemetrySample {
    double timeSeconds;
    std::string entityId;
    double latDeg;
    double lonDeg;
    std::optional<double> altitudeMeters;
    AltitudeReference altitudeReference;
    std::optional<double> headingDeg;
    std::optional<double> pitchDeg;
    std::optional<double> rollDeg;
    std::optional<double> speedMps;
};
```

Telemetry may use `geo_core` contracts. Terrain height sampling belongs in
`terrain_core`, but `terrain_core` must not depend on `telemetry_core`.
App-level code in `apps/animus` should combine resident terrain height data with
telemetry placement and expose diagnostics when terrain height is unavailable.

## 24. Build Phases and Milestones

Phase 0: project skeleton

Goal:

- CMake + Conan project builds on Linux
- `apps/animus` executable opens a GLFW window
- OpenGL context is created
- GLEW initialized
- OpenGL vendor/version printed
- debug context/logging enabled where available

Acceptance:

- app launches reliably
- black/clear-color screen appears
- no terrain yet
- GoogleTest runs at least one test binary

Phase 1: `geo_core` tile math

Goal:

- implement `TileCoord` and Web Mercator utilities
- unit tests for tile math

Acceptance:

- lat/lon to tile tested
- tile to bounds tested
- parent/child relationships tested
- no rendering changes needed

Phase 2: one flat imagery tile

Goal:

- load one local PNG imagery tile
- render it as a flat subdivided mesh

Acceptance:

- image orientation is correct
- UVs are correct
- camera can move
- no elevation yet
- no async yet

Phase 3: one displaced height tile

Goal:

- load one local elevation tile
- decode to `Raster<float>`
- generate CPU mesh
- render displaced terrain

Acceptance:

- min/max height printed
- no NaNs/spikes
- terrain height scale is plausible
- one tile renders correctly

Phase 4: height texture and hill shading

Goal:

- upload height raster as `GL_R32F` texture
- shader samples height texture
- fragment shader computes analytic normals
- imagery combines with hill shading

Acceptance:

- terrain has visible slope-based shading
- changing sun direction changes shading
- one tile still renders correctly

Phase 5: fixed 3x3 tile patch

Goal:

- render 3x3 neighboring tiles at same zoom
- use skirts or padded borders

Acceptance:

- no visible cracks between same-LOD neighbors
- imagery aligns across tile boundaries
- elevation aligns across tile boundaries

Phase 6: async tile loading

Goal:

- worker threads load/decode/build CPU meshes
- render thread uploads GPU resources
- GPU upload budget enforced

Acceptance:

- moving camera remains smooth
- no OpenGL calls from worker threads
- tile states visible in debug overlay

Phase 7: tile wishlist and scheduler

Goal:

- camera-driven tile selection
- desired tile set
- queue missing work
- prioritize center/near tiles
- cap jobs and resident tiles

Acceptance:

- moving camera requests correct tiles
- tile count is bounded
- no unbounded memory growth

Phase 8: no-holes parent fallback

Goal:

- parent tile remains visible until children are ready on GPU

Acceptance:

- no blank holes during zoom/pan
- children replace parent only after `ReadyGpu`
- fallback state is visible in debug overlay

Phase 9: cache hierarchy

Goal:

- memory cache
- disk cache
- stable cache keys
- LRU eviction

Acceptance:

- revisiting area is faster
- cache survives restart
- memory use remains bounded

Phase 10: synthesis

Goal:

- synthesize missing child from parent crop/scale
- synthesize missing parent from four children downsample
- persist synthesized tile

Acceptance:

- synthetic tiles are cached
- future requests hit cache
- no repeated synthesis loops

Phase 11: bathymetry/elevation merge

Goal:

- load elevation and bathymetry rasters
- merge into one height raster

Acceptance:

- renderer consumes one merged height field
- sea-level crossover works

Phase 12: minimal Animus app

Goal:

- host the native app shell after terrain core is stable
- host `terrain_core` renderer
- add layer controls and cache/debug panels

Acceptance:

- `terrain_core` stays independent of `apps/animus`
- app remains native desktop
- no web UI introduced

Phase 13: telemetry

Goal:

- add `telemetry_core`
- load simple CSV/JSON first
- later MCAP/Protobuf/HDF5
- render entities/tracks over terrain

Acceptance:

- one moving entity can play back over terrain
- track line renders
- terrain-relative placement works

## 25. Actionable Design Checklist

This checklist turns the architecture into a sequence of design and
implementation-ready work items. Each item should result in a clear artifact:
code, tests, a tool, a sample dataset, or a design note that lets the next item
proceed without rediscovering the same decisions.

Phase A, repository boundary and build foundation:

- [x] Create the self-contained `animus/` root as the future repository root,
  even if it initially lives inside this Altair checkout.
- [x] Keep `animus/` build files independent from Altair's top-level CMake.
  Altair currently has C99/C++11 build settings, while Animus requires C++20 or
  newer.
- [x] Do not add `add_subdirectory(animus)` to Altair unless a later integration
  task explicitly needs it.
- [x] Add `animus/CMakeLists.txt`, `animus/conanfile.py`, `animus/cmake/`,
  `animus/apps/`, `animus/libs/`, `animus/tests/`, `animus/tools/`,
  `animus/data/`, and `animus/cache/.gitkeep`.
- [x] Add root-level Animus developer docs for configure, build, run, test,
  dependency install, Linux/Nix/nixGL setup, and sample data handling.
- [x] Define dependency policy: terrain milestone dependencies are allowed;
  telemetry/video/archive/scripting dependencies remain delayed.
- [x] Add local verification scripts for independent Animus configure, build,
  unit tests, and basic formatting checks.
- [x] Add `.gitignore` or equivalent policy for generated caches, downloaded
  tiles, large terrain packs, build trees, and rendered/debug artifacts.

Phase B, contracts before rendering:

- [x] Add docs for module ownership and dependency direction inside
  `animus/docs/`.
- [x] Define public header layout and namespaces for `render_core`, `geo_core`,
  `terrain_core`, `data_core`, and delayed `telemetry_core`.
- [x] Define `LayerSpec`, `Raster`, `RasterFormat`, `SamplingMode`,
  `TileState`, and cache key conventions before tile loading code depends on
  them.
- [x] Define error-reporting conventions for tile source failures, decode
  failures, missing data, and GPU upload failures.
- [x] Add the first GoogleTest target so pure data/model code can be tested
  before any OpenGL work is required.

Phase C, geospatial math:

- [x] Implement `geo_core::TileCoord`, `GeoBounds`, stable tile keys, and hash
  support.
- [x] Implement Web Mercator `latLonToTile`, `tileToBounds`,
  `latLonToTileUv`, `parent`, and `children`.
- [x] Add edge-case tests near the date line and Web Mercator latitude limits.
- [x] Document coordinate conventions: XYZ addressing, UV origin, local render
  frame, and height units.

Phase D, sample data and offline tile preparation:

- [x] Choose a small sample area and exact zoom/tile IDs for early development.
- [x] Add `prepare_terrain_pack.py`, `inspect_tile.py`, and
  `validate_tile_pyramid.py` skeletons before runtime code depends on ad hoc
  data.
- [x] Define the local XYZ folder layout for imagery, elevation, and
  bathymetry.
- [x] Document accepted early formats: PNG imagery, Terrain-RGB-style PNG, and
  raw float32 height tiles.
- [x] Add validation for dimensions, coordinate coverage, sampling mode,
  min/max elevation, no-data values, and missing neighboring tiles.
- [x] Prepare or download a real Lake Tahoe pack under ignored local data paths
  and validate it with the Phase D tools before depending on it for rendering.

Phase E, native window and minimal render foundation:

- [x] Implement `apps/animus` as the native executable.
- [x] Open a GLFW window with an OpenGL Core Profile context.
- [x] Initialize GLEW and print OpenGL vendor, renderer, and version.
- [x] Enable debug context/logging where available.
- [x] Add only the `render_core` wrappers needed by the next terrain steps:
  window/context, shader compile/link, mesh, and render stats.
- [x] Keep `apps/animus` independent from telemetry and Altair/Bayek internals.

Phase F, single-tile and fixed-patch terrain:

- [x] Implement local XYZ folder imagery loading with libpng.
- [x] Render one flat subdivided imagery tile with correct orientation and UVs.
- [x] Implement Terrain-RGB or float32 elevation decode into meters.
- [x] Generate CPU terrain mesh from a height raster and print min/max height.
- [x] Upload `GL_R32F` height textures and implement shader hill shading.
- [x] Add terrain skirts or padded borders.
- [x] Render a fixed 3x3 same-LOD tile patch with aligned imagery and elevation.

Phase G, extraction into `terrain_core`:

- [x] Move reusable tile models, raster models, source interfaces, decoders,
  mesh generation, and terrain render coordination into `terrain_core` once the
  fixed patch works.
- [x] Keep `apps/animus` as a harness over `terrain_core`.
- [x] Add CPU-only tests for mesh generation, padding, Terrain-RGB decode, and
  tile-state transitions where practical.
- [x] Keep OpenGL-specific upload and draw code behind `render_core` or a narrow
  terrain render backend boundary.

Phase H, streaming and no-holes LOD:

- [x] Implement the explicit `TileState` state machine and debug colors.
- [x] Add worker threads for disk/cache lookup, decode, raster normalization,
  and CPU mesh generation.
- [x] Enforce the rule that workers never create OpenGL resources.
- [x] Define `PreparedTile` as the CPU-to-render-thread handoff object.
- [x] Add render-thread GPU upload queues and per-frame upload budgets.
- [x] Implement a camera-driven tile wishlist with bounded resident tiles and
  bounded outstanding jobs.
- [x] Prioritize center-screen or near-camera tile requests.
- [x] Implement parent fallback so parent tiles remain visible until all
  replacement children are `ReadyGpu`.
- [x] Show visible tiles, queued jobs, upload queues, failed tiles, and fallback
  states in the `apps/animus` debug panels.

Phase I, cache, synthesis, and richer terrain data:

- [x] Implement cache keys from `LayerSpec` identity plus `TileCoord`.
- [x] Add L0 resident GPU tile tracking, L1 CPU raster cache, and L3 disk tile
  cache with bounded memory use.
- [x] Add LRU eviction and cache hit/miss counters.
- [x] Implement local disk cache persistence and restart reuse.
- [x] Add missing-child synthesis from parent crop/scale.
- [x] Add missing-parent synthesis from four-child downsample.
- [x] Persist synthesized tiles and prevent repeated synthesis loops.
- [x] Add GDAL-backed GeoTIFF tile extraction after local XYZ tile loading is
  stable.
- [x] Add elevation/bathymetry merge into one renderer-facing height raster.

Phase J, full app shell after terrain stability:

- [x] Create `apps/animus` only after `terrain_core` is stable.
- [x] Host the terrain renderer in the app shell without introducing any web UI.
- [x] Add basic layer controls, cache panels, tile debug views, and render stats.
- [x] Keep app-level code dependent on `terrain_core`, not the other way around.
- [x] Keep `apps/animus` working as the terrain regression harness for every
  app-shell change.

Phase K, delayed telemetry:

- [x] Split `apps/animus/src/main.cpp` into app state, UI/debug panels, terrain
  rendering orchestration, and telemetry playback modules before adding more
  runtime complexity.
- [x] Introduce app-level state for layers, playback, selected entity/tile, and
  diagnostics without making `terrain_core` depend on UI.
- [x] Define `TelemetrySample`, `Entity`, `Track`, `Event`, `Timeline`, and
  `PlaybackClock`.
- [x] Add standalone MAVLink `.tlog` playback as the primary Phase K telemetry
  input.
- [x] Keep CSV/JSON as optional debug/export helpers.
- [x] Implement interpolation and timeline playback.
- [x] Render one moving entity over terrain.
- [x] Render track lines and event markers.
- [x] Reorganize developer panels into terrain, cache, render, telemetry,
  timeline, and entity inspection surfaces as telemetry arrives.
- [x] Use `terrain_core` height queries for terrain-relative placement.
- [x] Add live UDP, MCAP, Protobuf, and HDF5 support only after `.tlog`
  playback works.

Phase L, advanced terrain/data features:

- [ ] Add persisted UI/app config only after there are real preferences, recent
  files, layer presets, or project/session concepts.
- [ ] Add GeoTIFF overlays draped onto terrain with opacity and draw order.
- [ ] Add remote tile providers after local cache behavior is stable.
- [ ] Add SQLite/MBTiles metadata and cache indexing.
- [ ] Add cache prewarming tools for offline workflows.
- [ ] Add vertical datum/geoid correction metadata before mixing telemetry
  altitude and terrain height.
- [ ] Add visual regression artifact bundles or HTML reports once telemetry
  overlays, tracks, and events exist.
- [ ] Add PNG or export-friendly screenshot formats if captures become review
  artifacts; keep PPM for deterministic smoke checks.
- [ ] Add video/export workflows only after terrain and telemetry playback are
  stable.

Definition of fully designed:

- [ ] Every module has public headers, ownership notes, and dependency direction
  documented.
- [ ] Every runtime data path has an input format, normalized internal model,
  cache key, failure mode, and test strategy.
- [ ] Every render-thread boundary states what work is allowed on the render
  thread and what must remain on workers.
- [ ] Every phase has acceptance criteria that can be verified by tests, manual
  validation, or debug overlay evidence.
- [ ] The project can remain under `animus/` without depending on Altair/Bayek
  internals or web frontend technology.

## 26. Testing Strategy

`geo_core` tests:

- `TileCoord` equality/hash/key
- `latLonToTile`
- `tileToBounds`
- `latLonToTileUv`
- parent/children
- edge cases near date line and Web Mercator limits

`terrain_core` tests:

- `LayerSpec` cache key stability
- `Raster` construction
- sampling mode detection
- padding behavior
- Terrain-RGB decode
- elevation/bathymetry merge
- parent crop/scale synthesis
- child mosaic/downsample synthesis
- cache lookup priority
- tile state transitions where practical

`render_core` tests:

- mostly integration/manual because OpenGL depends on runtime GPU context
- shader compile checks where practical
- mesh generation tests can be CPU-only

`apps/animus` manual validation:

- one tile renders
- 3x3 patch seamless
- shading visible
- async loading does not stall
- parent fallback prevents holes
- cache reuse works

## 27. Debugging and Observability

`apps/animus` must have strong debug tools.

Required debug information:

- FPS/frame time
- camera location
- visible tile count
- queued tile count
- worker queue size
- GPU upload queue size
- cache hit/miss counts
- per-tile state coloring
- tile z/x/y labels or hover debug
- min/max elevation per selected tile
- memory usage estimates
- number of resident GPU textures/meshes
- number of failed tiles
- recent tile errors

Terrain streaming without debug visualization becomes impossible to tune.

## 28. Non-Goals for Early Milestones

Explicit early non-goals:

- no telemetry in the terrain runtime
- no final UI before terrain is stable
- no MCAP/Protobuf/HDF5 ingestion in terrain milestones
- no FFmpeg/video export in terrain milestones
- no remote tile provider in the first milestone
- no custom geoid correction in the first milestone
- no full bathymetry/elevation merge in the first milestone
- no attempt to render the whole planet initially
- no massive raw GeoTIFFs directly in the render loop
- no OpenGL work on worker threads
- no replacing a parent tile before children are GPU-ready

## 29. Data Preparation Strategy

Early terrain tests should use a small local tile pack.

Example:

```text
data/sample_area/
  imagery/
    12/
      654/
        1582.png
  elevation/
    12/
      654/
        1582.png or 1582.f32
```

Use Python/GDAL tools to prepare runtime-friendly tiles.

Tools to plan:

- `prepare_terrain_pack.py`
- `inspect_tile.py`
- `validate_tile_pyramid.py`
- `merge_elevation_bathymetry.py`

Runtime should consume normalized tiled data instead of giant raw datasets
during early milestones.

## 30. Agent Implementation Guidance

Rules for future agents:

- implement one phase at a time
- do not skip directly to the full app
- do not introduce telemetry until terrain phases are stable
- do not add dependencies casually
- do not move heavy work to the render thread
- do not create OpenGL resources on worker threads
- do not hide failures; expose tile errors in debug UI/logs
- add tests for pure math/data transformations
- keep `apps/animus` working as the regression harness
- keep core systems renderer-independent where practical
- keep the project self-contained under the top-level `animus/` folder so it
  can become a standalone repository

Good future task example:

```text
Implement geo_core TileCoord and Web Mercator utilities with GoogleTest
coverage. Do not touch rendering.
```

Bad future task example:

```text
Build the full terrain and telemetry app.
```

## 31. Final Architecture Summary

Animus should be built as a native C++20 Linux desktop application. The first
deliverable is not the full telemetry app but a terrain-only lab that proves
smooth streamed 3D terrain using OpenGL, GLFW, GLEW, GDAL, a strict tile state
machine, local tile caches, async workers, GPU upload budgeting, no-holes
parent fallback, mesh skirts/padded borders, and shader-based height-texture
hill shading.

Once terrain is stable, the reusable `terrain_core` library can be integrated
into the full Animus app. Telemetry ingestion, MCAP/Protobuf/HDF5 support,
tracks, entities, timeline playback, and video/export features should be
layered on top later.

The long-term project should remain contained under one `animus/` root so it can
be developed, packaged, and shipped as its own repository.

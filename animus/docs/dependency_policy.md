# Animus Dependency Policy

Animus should add dependencies only when a phase has a concrete target that
needs them. Phase B declares GoogleTest because it adds the first CTest target.

## Terrain-First Dependencies

These dependencies are acceptable for the early terrain renderer once a target
uses them:

- GLFW for native Linux window and OpenGL context creation.
- GLEW for OpenGL loading.
- GDAL for GeoTIFF, raster, and geospatial input.
- libpng for imagery and Terrain-RGB-style decoding.
- GoogleTest for unit tests.
- zlib, zstd, or lz4 for cache/data compression when cache formats need them.
- SQLite for MBTiles or cache metadata once that storage model exists.

## Delayed Dependencies

These dependencies should wait until their phase has a direct requirement:

- MCAP, Protobuf, and HDF5/HighFive for telemetry import and structured logs.
- FFmpeg and libaom-av1 for video/export workflows.
- yaml-cpp, tinyxml2, libkml, libarchive, and libcurl for richer config,
  import, archive, and remote-source workflows.
- Sol2 for scripting after the core terrain and app shell are stable.
- Dear ImGui, RenderDoc, and Tracy are developer/debug tools, not baseline
  Phase A runtime dependencies.

## Rule

Do not add a dependency because it is expected to be useful later. Add it in
the phase where a checked-in target, tool, or test has a direct need, and keep
the dependency local to that target where practical.

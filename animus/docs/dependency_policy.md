# Animus Dependency Policy

Animus should add dependencies only when a phase has a concrete target that
needs them. Phase B declares GoogleTest because it adds the first CTest target.

## Terrain-First Dependencies

These dependencies are acceptable for the early terrain renderer once a target
uses them:

- GLFW for native Linux window and OpenGL context creation.
- GLEW for OpenGL loading.
- GDAL for GeoTIFF, raster, and geospatial input.
- libpng for imagery and Terrain-RGB-style decoding, and for app-local PNG
  framebuffer capture artifacts.
- libjpeg-turbo for MBTiles JPEG tile payload decoding.
- libcurl for explicitly configured runtime remote tile providers. Animus must
  not perform network access unless a provider URL or prewarm/download command
  is supplied by the user.
- SQLite for MBTiles input and cache metadata.
- PROJ for explicit vertical grid shift datum correction.
- GoogleTest for unit tests.
- zlib, zstd, or lz4 for cache/data compression when cache formats need them.
- FFmpeg as an optional tool/runtime dependency for explicit MP4 export
  workflows. Default build and test must not require building FFmpeg.
- Asio as a header-only dependency for direct live MAVLink UDP ingest. Keep it
  scoped to `telemetry_live` and app targets that explicitly consume live
  networking.

## Delayed Dependencies

These dependencies should wait until their phase has a direct requirement:

- yaml-cpp, tinyxml2, libkml, libarchive, and libcurl for richer config,
  import, archive, and remote-source workflows.
- Sol2 for scripting after the core terrain and app shell are stable.
- Dear ImGui, RenderDoc, and Tracy are developer/debug tools, not baseline
  Phase A runtime dependencies.

## Rule

Do not add a dependency because it is expected to be useful later. Add it in
the phase where a checked-in target, tool, or test has a direct need, and keep
the dependency local to that target where practical.

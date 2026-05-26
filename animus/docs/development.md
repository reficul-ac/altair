# Animus Development

## Platform

Animus targets native Linux desktop development. The `apps/animus` executable
owns the OpenGL terrain runtime, so local machines need working OpenGL driver
access plus the platform development files discovered by CMake.

On Debian/Ubuntu-style systems, useful baseline packages include `cmake`,
`ninja-build`, `build-essential`, `pkg-config`, `libgl1-mesa-dev`, and the
X11/Wayland development libraries required by GLFW. Conan supplies GLFW, GLEW,
and GoogleTest for the Animus build.

OpenGL smoke runs need a display server or a virtual framebuffer. Use
`xvfb-run` for headless CI/local smoke checks when hardware display access is
not available:

```bash
xvfb-run -a animus/build/apps/animus/animus --smoke --frames 120 \
  --capture-ppm /tmp/animus_smoke.ppm
```

Nix users may need `nixGL` or an equivalent GPU wrapper when running native
OpenGL applications, depending on how the system graphics drivers are exposed.

## Build Directories

Use `animus/build/` for the default Debug build:

```bash
conan install animus -of animus/build --build=missing -s build_type=Debug \
  -s compiler.cppstd=20
cmake -S animus -B animus/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=animus/build/conan_toolchain.cmake
cmake --build animus/build
ctest --test-dir animus/build --output-on-failure
```

Additional local build directories should stay under `animus/` and match
`build-*` or `cmake-build-*` so they remain ignored.

## Generated Data

Generated caches, downloaded tiles, large raster inputs, screenshots, captures,
plots, rendered artifacts, and logs are local artifacts. Keep them under the
ignored paths documented in `animus/.gitignore`. The only tracked cache entry in
Phase A is `animus/cache/.gitkeep`.

Small deterministic fixtures may be promoted into version control in later
phases when tests or offline demos require them.

## Python Tool Dependencies

Phase D terrain-pack tools use Pillow for PNG and Terrain-RGB inspection. Install
the Python tool requirements into your preferred environment before validating
PNG tile packs:

```bash
python3 -m pip install -r animus/tools/requirements.txt
```

The tools emit an actionable error if Pillow is missing and PNG inspection is
requested.

## Verification

Run the local verification wrapper from any working directory:

```bash
python3 /path/to/altair/animus/tools/verify_animus.py
```

The script locates the `animus/` root from its own path, configures CMake with
Ninja into `animus/build/`, builds, and runs CTest. It runs `conan install` for
declared C++ dependencies, but it does not download terrain data or generated
runtime artifacts.

OpenGL smoke tests are opt-in because they require a working graphics context:

```bash
cmake -S animus -B animus/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=animus/build/conan_toolchain.cmake \
  -DANIMUS_ENABLE_OPENGL_TESTS=ON
ctest --test-dir animus/build -R animus_app_opengl_smoke --output-on-failure
```

Run the native executable directly with:

```bash
animus/build/apps/animus/animus
animus/build/apps/animus/animus --smoke --frames 120 --capture-ppm /tmp/animus_smoke.ppm
animus/build/apps/animus/animus --smoke --frames 120 --capture-png /tmp/animus_smoke.png
```

Normal mode opens a GLFW window with the terrain developer console and exits on
Escape or window close. Smoke mode still creates a real OpenGL context, renders
the requested frame count, prints OpenGL vendor, renderer, OpenGL version, GLSL
version, writes captures when requested, and exits nonzero if context creation,
GLEW initialization, shader compilation, linking, streaming, upload, or drawing
setup fails.

## Screenshot Capture

Use Animus' built-in framebuffer capture for repeatable smoke screenshots.
Binary PPM is the deterministic smoke format. PNG is available for
review-friendly artifacts. Captures are written after the requested frame has
been rendered and before the buffer swap:

```bash
xvfb-run -a animus/build/apps/animus/animus --smoke --frames 120 \
  --capture-ppm artifacts/animus/screenshots/animus_smoke.ppm \
  --capture-png artifacts/animus/screenshots/animus_smoke.png
```

For pixel-stable terrain-only comparisons, disable the developer console so
frame-time text and cache counters do not change the image:

```bash
xvfb-run -a animus/build/apps/animus/animus --smoke --frames 120 \
  --no-debug-overlay --capture-ppm artifacts/animus/screenshots/terrain_only.ppm
```

Validate a captured PPM without extra dependencies:

```bash
python3 -c "from pathlib import Path; p=Path('artifacts/animus/screenshots/terrain_only.ppm'); d=p.read_bytes(); h=d.split(b'\n',3); px=h[3]; print(h[0].decode(), h[1].decode(), len(set(px)), min(px), max(px))"
```

The unique-value count should be greater than one and the min/max values should
not be identical. Store captures under ignored artifact paths such as
`artifacts/animus/screenshots/` unless a future test promotes a small fixture
into source control.

For review bundles, `verify_animus.py` can generate a deterministic telemetry
log, PPM capture, PNG capture, JSON manifest, and tiny HTML report:

```bash
python3 animus/tools/verify_animus.py --screenshot-smoke --artifact-bundle
python3 animus/tools/verify_animus.py --skip-build --artifact-bundle --overlay-smoke
```

When running interactively with the developer workspace enabled, use the
`Capture` tab to edit a PNG output path and press `Save PNG`. The default manual
path is `artifacts/animus/screenshots/manual_screenshot.png`.

## Overlays, Datum, And Export

GeoTIFF overlays are explicit app inputs. `--overlay-geotiff` remains the
compatibility flag for the first overlay; repeat `--overlay PATH` for additional
layers. `--overlay-opacity` and `--overlay-order` configure the compatibility
overlay, and the app persists the overlay list in its JSON config.

Telemetry ellipsoid altitude correction is also explicit. Download or provide a
PROJ vertical grid under an ignored data path, then pass it with
`--geoid-grid PATH`. The helper below performs the network action only when run:

```bash
python3 animus/tools/download_geoid_grid.py --dry-run
python3 animus/tools/download_geoid_grid.py --output-dir animus/data/geoid
```

Video export is available from both the CLI and the in-app `Capture` tab. The
CLI writes a deterministic PNG sequence, then `ffmpeg` encodes MP4.
H.264/yuv420p is the default review-compatible output; AV1 is opt-in when the
installed FFmpeg supports `libaom-av1`.

```bash
python3 animus/tools/export_video.py --frames 180 --fps 30 \
  --output artifacts/animus/export/smoke.mp4
```

When running the app interactively, use the `Capture` tab to edit the MP4 path
and toggle `Record MP4`. The app records PNG frames during capture and encodes
the configured MP4 path when recording stops.

# Animus

Animus is the future standalone native Linux terrain and telemetry
visualization project. It currently lives under this Altair checkout only to
bootstrap the repository boundary; it is not part of the Altair build.

## Phase Status

Phase A established the independent project root, build files, documentation,
verification entrypoint, and generated-data ignore policy. Phases B through I
add pure model contracts, tile math, terrain data contracts, offline tile
tooling, reusable terrain streaming/cache systems, and the native
`apps/animus` developer console around that terrain runtime.

## Build And Test

From the Altair checkout root:

```bash
python3 animus/tools/verify_animus.py
```

Manual equivalent:

```bash
conan install animus -of animus/build --build=missing -s build_type=Debug \
  -s compiler.cppstd=20
cmake -S animus -B animus/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=animus/build/conan_toolchain.cmake
cmake --build animus/build
ctest --test-dir animus/build --output-on-failure
```

The contract test target uses GoogleTest from Conan. Phase E also uses GLFW and
GLEW from Conan plus the platform OpenGL library found by CMake.

Run the native render smoke after building:

```bash
animus/build/apps/animus/animus --smoke --frames 120 --capture-ppm /tmp/animus_phase_j.ppm
```

Use `xvfb-run -a` before the command on headless Linux systems.
Use `--no-debug-overlay` with `--capture-ppm` for more stable terrain-only
screenshots.

## Repository Boundary

- `animus/` is treated as the root of a future standalone repository.
- Animus build files must not depend on Altair's top-level CMake, targets, or
  helper functions.
- Altair's root `CMakeLists.txt` must not add `add_subdirectory(animus)` during
  Phase A.
- Animus code may not assume Bayek or Altair headers, targets, generated
  artifacts, or vehicle-specific behavior.
- Generated terrain data, caches, captures, logs, and rendered artifacts stay
  out of version control unless a later phase explicitly promotes a small
  fixture into the source tree.

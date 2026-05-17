# Altair Testing Strategy

Altair tests verify vehicle-specific behavior and the integration between Altair and the Bayek submodule. Bayek framework-level testing expectations live in [Bayek Testing Strategy](../bayek/docs/testing.md).

## Unit Tests

Altair unit tests cover:

- Altair mixer saturation and safe outputs

Bayek common math and control tests should live in the Bayek repository as that test suite grows.

## Integration Tests

Integration tests cover:

- deterministic replay of `altair_fsw_step()`
- SITL smoke run with bounded actuator outputs
- Monte Carlo smoke summary semantics and metric gates
- telemetry encode/decode and CRC rejection
- Bayek host SITL parsing/condition machinery without Altair symbols
- Bayek-to-Altair vehicle interface routing through Altair mixer limits

The deterministic replay test resets the core and replays the same input sequence, then checks that actuator outputs match within a tight tolerance.

The SITL smoke test is not intended to prove aircraft performance. It proves that the host plant, sensor generation, Bayek FSW call path, Altair vehicle interface, and actuator bounds are wired correctly.

## Performance Test

The performance test initializes Bayek with `altair_vehicle_interface()`, calls `altair_fsw_step()` many times, and prints timing. It uses a conservative threshold to catch gross regressions only.

Early in the project, performance numbers are more useful as trend data than hard certification gates. The threshold should only become stricter after target hardware, loop rate, and control complexity are better understood.

## CI Recommendation

The repository includes a GitHub Actions workflow at `.github/workflows/ci.yml`. It runs:

```sh
python3 tools/python/format_repo.py --check
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-animus-qt --target animus_qt animus_qt_unit_tests --parallel
ctest --test-dir build-animus-qt --output-on-failure -R animus_qt
python3 tools/python/capture_animus_qt_sitl.py
```

The formatting check requires `clang-format`, `black`, and `cmake-format`. Use check mode in CI
and before review:

```sh
python3 tools/python/format_repo.py --check
```

Use fix mode for local rewrites:

```sh
python3 tools/python/format_repo.py --fix
```

To block merges on GitHub, configure a branch protection rule or repository ruleset for the default branch and require the `format-check`, `cmake-test`, and `animus-qt` status checks to pass before merging.

Optional later checks:

- compiler warnings as errors
- sanitizer builds for host-only tests
- replay artifact diffing
- PlatformIO compile check
- static analysis

## Animus UI Verification

Animus UI and layout changes require Qt build, unit, and screenshot verification:

```sh
cmake -S . -B build-animus-qt -DALTAIR_BUILD_ANIMUS_QT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-animus-qt --target animus_qt animus_qt_unit_tests --parallel
ctest --test-dir build-animus-qt --output-on-failure -R animus_qt
python3 tools/python/capture_animus_qt_sitl.py
```

The Qt capture launches the built `animus_qt` executable once per workspace. If `DISPLAY` is already set, the capture uses that display unchanged. If no display is present, the helper starts `Xvfb` directly with a managed virtual display and passes the allocated `DISPLAY` to each capture process. Constrained environments without a usable `Xvfb` fall back to Qt offscreen rendering with the software backend, and the manifest records that fallback. It captures `map-2d`, `terrain-3d`, and `setup` with deterministic mock telemetry, writes PNGs under `artifacts/animus-qt-screenshots/<timestamp>/screenshots/`, records per-workspace logs, and fails when a PNG is missing, zero-sized, visually blank, the wrong size, or too similar to another workspace capture. `visual-report.md` and `run-manifest.json` also include region diagnostics for toolbar/tab visibility, workspace content variation, and color diversity warnings. The `--no-run` flag remains available for dependency-light readiness checks only.

Use `python3 tools/python/verify_agent_work.py --animus-qt` for the same local bundle. The deprecated `--animus` flag is an alias for `--animus-qt`; `--all` includes the Qt Animus lane only.

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

To block merges on GitHub, configure a branch protection rule or repository ruleset for the default branch and require the `format-check` and `cmake-test` status checks to pass before merging.

Optional later checks:

- compiler warnings as errors
- sanitizer builds for host-only tests
- replay artifact diffing
- PlatformIO compile check
- static analysis

## Animus UI Verification

Animus UI and layout changes require local screenshot verification in addition to automated tests:

```sh
npm test --prefix tools/animus
npm run build --prefix tools/animus
python3 tools/python/capture_animus_sitl.py
```

The capture workflow starts a no-QGC live SITL session, opens Animus through Electron, captures the `flight`, `map`, and `inspector` workspaces at `1440x900`, and writes screenshots plus logs under `artifacts/animus-screenshots/<timestamp>/`.

Inspect the generated screenshots before review. Check that the 3D scene is nonblank, live link/telemetry state is visible, the selected workspace matches the file name, desktop layout is usable, and controls/text are not obviously clipped or overlapping. This workflow is required for Animus UI/layout changes even before it becomes a GitHub Actions status check; if it cannot run, note the reason and the residual visual risk in the review or final response.

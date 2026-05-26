# Altair Testing Strategy

Altair tests verify vehicle-specific behavior and the integration between Altair and the Bayek submodule. Bayek framework-level testing expectations live in [Bayek Testing Strategy](../bayek/docs/testing.md).

## Unit Tests

Altair unit tests cover:

- Altair parameters and vehicle interface behavior
- Altair mixer saturation and safe outputs
- Altair simulation parameters/model wiring
- SITL case, condition, initial-condition, and trim policy

Bayek common math and control tests should live in the Bayek repository as that test suite grows.

## Integration Tests

Integration tests cover:

- deterministic replay of `altair_fsw_step()`
- SITL smoke run with bounded actuator outputs
- Monte Carlo smoke summary semantics and metric gates
- Altair telemetry contract manifest and generator checks
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

## Generated Artifact Cleanup

Builds, SITL runs, map cache operations, and verification helpers intentionally write
ignored artifacts under repo-local paths such as `build/`, `build-*`,
`artifacts/`, `plots/`, `map_cache/`, root `*.csv`, `sitl_3d.html`, and
`__pycache__/` trees. Preview the cleanup before deleting anything:

```sh
python3 tools/python/clean_generated_artifacts.py
```

Apply the cleanup explicitly:

```sh
python3 tools/python/clean_generated_artifacts.py --apply
```

The helper only removes paths in that generated-artifact allowlist when Git also
reports them as ignored, so tracked fixtures such as replay CSVs are preserved.

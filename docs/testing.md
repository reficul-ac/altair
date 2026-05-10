# Testing Strategy

The initial test suite is intentionally focused on deterministic behavior and boundary correctness.

## Unit Tests

Unit tests cover:

- vector math
- quaternion normalization, multiplication through rotation, and Euler conversions
- angle wrapping, clamp, and interpolation
- low-pass filter behavior
- PID output saturation and integrator limits
- explicit integrator anti-windup
- rate and slew limiting
- deadband
- Altair mixer saturation and safe outputs

These tests keep low-level numerical behavior visible and separate from flight-mode behavior.

## Integration Tests

Integration tests cover:

- deterministic replay of `fsw_step()`
- SITL smoke run with bounded actuator outputs
- telemetry encode/decode and CRC rejection

The deterministic replay test resets the core and replays the same input sequence, then checks that actuator outputs match within a tight tolerance.

The SITL smoke test is not intended to prove aircraft performance. It proves that the host plant, sensor generation, FSW call path, and actuator bounds are wired correctly.

## Performance Test

The performance test calls `fsw_step()` many times and prints timing. It uses a conservative threshold to catch gross regressions only.

Early in the project, performance numbers are more useful as trend data than hard certification gates. The threshold should only become stricter after target hardware, loop rate, and control complexity are better understood.

## CI Recommendation

The repository includes a GitHub Actions workflow at `.github/workflows/ci.yml`. It runs:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To block merges on GitHub, configure a branch protection rule or repository ruleset for the default branch and require the `cmake-test` status check to pass before merging.

Optional later checks:

- compiler warnings as errors
- sanitizer builds for host-only tests
- replay artifact diffing
- PlatformIO compile check
- formatting and static analysis

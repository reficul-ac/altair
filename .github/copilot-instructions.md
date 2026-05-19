<!-- Copilot/AI agent instructions for the Altair repository. Keep this short, actionable, and codebase-specific. -->
# Copilot instructions — Altair

These instructions give targeted guidance for an AI coding agent working in the Altair vehicle repository (root) and its reusable C99 framework `bayek/`.

- Work directly on `main` unless the user explicitly asks to create branches or PRs.
- Preserve the `bayek/` boundary: Bayek is a vehicle-agnostic C99 framework. Do not add Altair headers, board assumptions, or vehicle parameters into `bayek/`.

Key developer commands (run from repository root):

- Build and run tests (host):

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- Release build with warnings-as-errors (use when touching shared C or build settings):

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DALTAIR_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

- Formatting check & fix (C/C++, CMake, Python):

```bash
python3 tools/python/format_repo.py --check
python3 tools/python/format_repo.py --fix   # to rewrite tracked files locally
```

- SITL quick smoke run (writes CSV):

```bash
./build/vehicle/sitl_runner --scenario smoke --output sitl_smoke.csv
```

- Run deterministic 6DOF SITL with initial conditions and plotting: see `tools/python/run_sitl.py` and `README.md` for the sample ini and flags (e.g. `--realtime`, `--mavlink`, `--frame-mode ned`).

What to read first (high-signal files & dirs):

- `AGENTS.md` (root): repository-wide agent guidance, verification commands, Animus UI rules.
- `bayek/AGENTS.md`: Bayek-specific guidance and portability constraints (no Altair assumptions).
- `README.md` and `docs/README.md`: overall architecture, targets, and common runner commands.
- `vehicle/altair_fsw.c`, `vehicle/altair_vehicle.c`: where vehicle orchestration and the `altair_vehicle_interface()` binding live.
- `bayek/` directory: reusable math, controllers, sim helpers — keep changes here generic and deterministic.

Essential architecture summary (big-picture):

- Altair (this repo) provides vehicle-specific parameters, mixers, and the FSW glue that binds Bayek's reusable modules to the specific aircraft. Key integration point: `altair_vehicle_interface()`.
- Bayek (subdirectory) is the framework layer: C99 math/types, control utilities, deterministic sim and SITL helpers, telemetry encoding/decoding. Bayek must remain vehicle-agnostic and avoid host-only APIs in portable flight paths.
- Build targets indicate responsibilities: `altair_vehicle`, `altair_sim_model`, `sitl_runner`, `mc_runner`, `bayek_*` targets in the top-level CMake files.

Project-specific conventions and patterns:

- Deterministic, portable C: prefer caller-owned structs, explicit state, avoid heap allocation in flight software paths. `real_t` is used in Bayek (as `float`) — preserve that assumption.
- No hidden global state in Bayek. If persistent state is needed, prefer explicit structs passed by the caller.
- Use `tools/python/*` helpers for verification tasks (formatting, SITL runner wrappers, verification wrapper `verify_agent_work.py`).
- Treat `TODO.md` as the active backlog. After finishing work, update it: check off resolved items and add concise follow-ups for any remaining issues discovered.

Testing and verification guidance:

- Prefer focused unit tests for Bayek math/control/simulation helpers. Use integration tests and SITL scenarios from the repo root for vehicle integrations.
- Use the helper wrapper to bundle verification artifacts:

```bash
python3 tools/python/verify_agent_work.py --format --cmake
python3 tools/python/verify_agent_work.py --sitl-plots
python3 tools/python/verify_agent_work.py --all
```

- When modifying Animus UI or `tools/animus-qt/**`, run the Animus Qt verification workflow from `AGENTS.md` (build-animus-qt, capture screenshots, inspect `artifacts/animus-qt-screenshots/`).

Integration points & external behaviors to watch for:

- MAVLink / QGC bridging is exercised by `tools/python/run_sitl_session.py`. SITL telemetry is forwarded to `127.0.0.1:14550` by default.
- CSV schema and runner policies are Altair-owned: changes to CSV columns, runner defaults, or Monte Carlo summary semantics belong to Altair.
- Bayek provides telemetry encode/decode helpers; keep packet formats vehicle-agnostic where possible.

Do's and Don'ts (concise):

- Do: preserve Bayek portability, run formatting and CTest, update `TODO.md` with follow-ups.
- Don't: add vehicle-specific constants into `bayek/`, rely on host-only APIs in portable FSW paths, or skip Release warnings-as-errors when changing shared C or build settings.

Examples from the codebase (patterns to follow):

- Vehicle interface binding: see `vehicle/altair_vehicle.c` for how Altair binds into Bayek via `altair_vehicle_interface()`.
- SITL runner usage: `./build/vehicle/sitl_runner --scenario smoke --output sitl_smoke.csv` and `tools/python/run_sitl.py` for higher-level runs and plots.

When to ask for clarification:

- If a requested change affects Bayek's API surface, ask whether the change belongs in Bayek or Altair (Bayek must stay vehicle-agnostic).
- If a requested verification step requires hardware or an external service not available in CI, report the limitation and suggest the closest local checks (SITL, Animator capture, or unit tests).

If anything here is unclear or you'd like a different level of detail (more examples, expanded Animus UI checklist, or a longer build matrix), tell me which sections to expand.

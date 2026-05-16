# Agent Guidance

Altair is the vehicle repository. Bayek is the reusable C99 framework submodule
under `bayek/`. Keep that boundary explicit when adding code, tests, docs, or
tools.

## Workflow

Work directly on `main` during development. Do not create branches or pull
requests unless the user explicitly asks for them.

Before changing behavior, read the nearby code and the relevant docs under
`docs/` and `bayek/docs/`. Prefer the existing CMake, C99, Python, and Animus
patterns over new local conventions.

Keep changes simple, deterministic, centralized where policy belongs, and
compatible with eventual real hardware use. Avoid hidden global state, heap use
in flight software, nondeterministic time sources in deterministic paths, and
host-only dependencies in portable code.

## Backlog Hygiene

Treat `TODO.md` as the active repository backlog. At the end of every work item,
including Plan mode investigations, review `TODO.md` before finalizing:

- check off any item that the completed work fully resolves
- add a concise unchecked item for any real bug, missing test, or improvement
  discovered during the work but left outside the current scope
- avoid duplicates; update an existing item when that is clearer than adding a
  new one

Keep entries specific enough for a future agent to act on, and place them under
the closest existing heading.

## Repository Boundaries

Altair owns vehicle-specific integration:

- airframe parameters, vehicle limits, and simulation parameter values
- mixer behavior, actuator policy, failsafe/disarmed outputs, and board wiring
- the `altair_vehicle_interface()` implementation and Altair FSW facade
- SITL scenarios, case files, CSV schemas, runner defaults, and guardrail policy
- Monte Carlo runner policy and summary CSV semantics
- Animus operator UI, live workflow presentation, and vehicle-facing tests

Bayek owns reusable framework code:

- C99 common types, math, control utilities, and deterministic helpers
- portable FSW domains and caller-owned state APIs
- generic simulation, fixed-wing dynamics and trim helpers
- vehicle-agnostic host SITL parsing and condition machinery
- telemetry packet encode/decode helpers

Files under `bayek/` must not include Altair headers, link Altair targets, or
assume a specific aircraft. Altair may depend on Bayek and should provide
vehicle-specific behavior through clear interfaces rather than modifying Bayek
to know about Altair.

## Verification

Choose checks based on the files and behavior changed. Useful commands:

```bash
python3 tools/python/format_repo.py --check
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DALTAIR_WARNINGS_AS_ERRORS=ON
python3 tools/python/run_sitl.py --scenario cruise6dof --initial tests/integration/cruise6dof_initial.ini --duration 60 --dt 0.01 --output sitl_cruise6dof.csv --plot all --plots-dir plots/sitl
./build/vehicle/mc_runner --seed 1 --runs 100 --scenario smoke --output mc_summary.csv
```

Use the helper wrapper when a concise artifact bundle is useful:

```bash
python3 tools/python/verify_agent_work.py --format --cmake
python3 tools/python/verify_agent_work.py --sitl-plots
python3 tools/python/verify_agent_work.py --all
```

The wrapper writes logs, generated SITL CSVs, plots, Monte Carlo output, and a
JSON manifest under `artifacts/agent-verification/<timestamp>/`. It wraps the
existing tools only; it does not replace direct CTest, npm, SITL, or capture
workflows.

Run a Release warnings-as-errors configure/build/test when touching shared C,
build settings, or code likely to surface compiler diagnostics:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DALTAIR_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

Run SITL plots for flight, simulation, telemetry CSV, or runner behavior changes.
Run the Monte Carlo smoke for runner, dispersions, guardrail, or summary CSV
changes.

## Animus UI Verification

Agents modifying `tools/animus/src/**`, `tools/animus/src/styles/**`, or Animus
shell/layout behavior must run the Animus UI verification workflow before
finalizing:

```bash
npm test --prefix tools/animus
npm run build --prefix tools/animus
python3 tools/python/capture_animus_sitl.py
```

Inspect the generated screenshots under
`artifacts/animus-screenshots/<timestamp>/` before reporting the work complete.
Treat this capture workflow like a pre-merge check for Animus UI changes even
when it is not represented as a GitHub Actions status check.

For deeper interaction changes, run:

```bash
python3 tools/python/interact_animus_sitl.py
```

Use `capture_animus_sitl.py` for broad workspace screenshot verification. Use
`interact_animus_sitl.py` for direct UI interaction, dashboard widget workflows,
workspace switches, guarded command/control gating, replay/session controls, and
arbitrary checkpoint screenshots. The interaction workflow writes
`run-manifest.json`, service logs, Playwright artifacts, and screenshots under
`artifacts/animus-interactions/<timestamp>/`.

Final responses for Animus UI changes must mention the screenshot artifact
directory and any visual issues found. If the capture workflow cannot run, say
why and describe the remaining visual risk.

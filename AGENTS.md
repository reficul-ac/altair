# Agent Guidance

Agents modifying `tools/animus/src/**`, `tools/animus/src/styles/**`, or Animus shell/layout behavior must run the Animus UI verification workflow before finalizing:

```bash
npm test --prefix tools/animus
npm run build --prefix tools/animus
python3 tools/python/capture_animus_sitl.py
```

Inspect the generated screenshots under `artifacts/animus-screenshots/<timestamp>/` before reporting the work complete. Treat this capture workflow like a pre-merge check for Animus UI changes even when it is not represented as a GitHub Actions status check.

Final responses for Animus UI changes must mention the screenshot artifact directory and any visual issues found. If the capture workflow cannot run, say why and describe the remaining visual risk.

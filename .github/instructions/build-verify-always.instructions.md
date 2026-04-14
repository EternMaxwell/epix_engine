---
applyTo: "**"
description: "Always-on workflow rule: build, run tests, run examples, and verify after any code modification. Use when finishing any coding task, making changes, or preparing to end work."
---
# Build and Verify After Every Modification

After making any complete code changes, **always** build the relevant targets and run available tests before considering the work complete:

1. **Build**: Compile the affected targets (e.g., `cmake --build build --target <target>`).
2. **Run tests**: Execute unit tests, integration tests, or regression tests that cover the changed code.
3. **Run examples**: If the work involved an example or the example exercises the changed code, run it to confirm it starts and behaves correctly. Set the `SPDLOG_LEVEL` environment variable to `trace` (or `debug`) before launching so the output is verbose enough to judge correctness (or its better to test in default, e.g. `info` level, and switch to `debug/trace` if you didn't see any error, or need detailed info, you can also add a custom sink to exit on error, and use that temporaryly for testing):
   ```powershell
   $env:SPDLOG_LEVEL = "trace"
   .\build\bin\<example_name>.exe   # or the appropriate output path
   ```
   After confirming the example runs correctly, **stop the process** — do not leave it running. Use `Stop-Process`, `Ctrl+C` forwarded via `send_to_terminal`, or `kill_terminal` as appropriate.
4. **Verify**: Confirm there are no new build errors, test failures, example crashes, or regressions introduced.

Do not stop at static inspection alone. If a build, test, or example run fails, fix it before finishing.
If no dedicated test target exists for the modified code, at minimum build the affected target and run the closest available test suite.

---
applyTo: "**"
description: "Always-on workflow rule: build, run tests, run examples, and verify after any code modification. Use when finishing any coding task, making changes, or preparing to end work."
---
# Build and Verify After Every Modification

After finishing a logical unit of code modification (for example, implementing a function, fixing a bug, or completing a refactor) and before reporting the task as done, build and verify the affected code:

If the locally available build paths distinguish `import std` and `no import std` configurations, apply the verification workflow below to both variants: one pass for the existing configured `import std` build path and one pass for the existing configured `no import std` build path. These build paths may come from CMake presets or from each developer's own local configuration. Prefer already configured build paths; do not configure a missing counterpart only to satisfy this rule. If only one of the two build paths is configured locally, run verification once with the available build path.

1. **Build**: Compile the affected targets (e.g., `cmake --build build --target <target>`):
   - the target that directly uses the modified files;
   - its associated test target;
   - first-level targets that depend on it.
   However with cmake-tools extension, we always need to build the whole project if want ctest, this is acceptable.
2. **Run tests**: Execute unit tests, integration tests, or regression tests that cover the changed code.
3. **Run examples**: If the work involved an example or the example exercises the changed code, run it with `SPDLOG_LEVEL=trace` and verify that it exits with code 0 or, for long-running examples, runs without crashes or `ERROR`/`CRITICAL` log messages long enough to exercise the changed behavior:
   ```powershell
   $env:SPDLOG_LEVEL = "trace"
   .\build\bin\<example_name>.exe   # or the appropriate output path
   ```
   ```shell
   export SPDLOG_LEVEL=trace
   ./build/bin/<example_name>       # or the appropriate output path
   ```
   After confirming the example runs correctly, **stop the process** — do not leave it running. Use `Stop-Process`, `Ctrl+C` forwarded via `send_to_terminal`, or `kill_terminal` as appropriate.
4. **Verify**: Confirm there are no new build errors, test failures, example crashes, or regressions introduced. If only one configured local `import std`/`no import std` build path was available, report the missing counterpart here.

Do not stop at static inspection alone. If a build, test, or example run fails, fix it before finishing.
If no dedicated test target exists for the modified code, build the affected target and run the closest available test suite; this satisfies the test step for that change.

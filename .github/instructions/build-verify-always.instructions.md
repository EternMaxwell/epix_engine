---
applyTo: "**"
description: "Always-on workflow rule: build, run tests, and verify after any code modification. Use when finishing any coding task, making changes, or preparing to end work."
---
# Build and Verify After Every Modification

After making any complete code changes, **always** build the relevant targets and run available tests before considering the work complete:

1. **Build**: Compile the affected targets (e.g., `cmake --build build --target <target>`).
2. **Run tests**: Execute unit tests, integration tests, or regression tests that cover the changed code.
3. **Verify**: Confirm there are no new build errors, test failures, or regressions introduced.

Do not stop at static inspection alone. If a build or test fails, fix it before finishing.
If no dedicated test target exists for the modified code, at minimum build the affected target and run the closest available test suite.

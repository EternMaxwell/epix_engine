# TODO — time

Features that are planned, partially implemented, or have an API stub only.
See also: [project-wide todo](../todo.md)

The `epix.time` module is fully implemented. All exported functions have real bodies in `src/time.cpp`. A scan of all `modules/**/*.cppm` and `src/time.cpp` found no `// TODO`, `// FIXME`, `assert(false)`, empty bodies, or commented-out declarations.

Potential future work (no interface stubs exist yet):

- [ ] **`Time<Fixed>` rate resource** — no API to query "how many times did FixedMain run this frame?" from outside the fixed loop. `times_finished_this_tick()` exists only on `Timer`, not as a global counter.
- [ ] **`TimeUpdateConfig` ergonomics** — currently requires setting fields before the `First` schedule runs. No helper method or builder pattern for common configurations.
- [ ] **Virtual time in `on_timer` during pause** — `on_timer` is driven by `Time<>` which copies virtual time; when paused `Time<>` stops advancing so periodic conditions never fire. There is no built-in "fire every N seconds but keep ticking while paused" condition — users must use `on_real_timer` instead.

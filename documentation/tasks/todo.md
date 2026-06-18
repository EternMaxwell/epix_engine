# TODO — tasks

Features that are planned, partially implemented, or have a stub-only implementation.  
See also: [project-wide todo](../todo.md)

- [i] **`ThreadExecutorTicker::try_tick()`** — stub, always returns `false`. The comment in `thread_executor.cppm` states: *"single_thread_context runs on its own thread, so we can't 'tick' from outside. This is a no-op placeholder that returns false."* No implementation exists.

- [i] **`Scope::spawn_on_external`** — does not use a separate external executor; it delegates directly to `spawn`. The comment in `task_pool.cppm` states: *"Currently delegates to spawn (no separate external executor)."* Matching bevy's `Scope::spawn_on_external` behaviour (offload to a provided external executor) is not implemented.

- [ ] **`TaskPoolBuilder::stack_size`** — the `stack_size` field is stored in `TaskPoolBuilder` but is never passed to the backend. Neither `asio::thread_pool` nor the manually managed `IoBundle` threads honour the configured stack size.

# Task TODO

The main task, pool, local-executor, scoped-work, and thread-executor APIs are
implemented. The remaining compatibility gaps are:

- `TaskPoolBuilder::thread_name`, `on_thread_spawn`, and `on_thread_destroy`
  retain their values for source compatibility but are no-ops with the current
  stdexec-backed pool implementations.
- `TaskPool::scope_with_executor` accepts the Bevy-style selection arguments but
  currently ignores them and delegates to `scope`.
- `TaskPool::with_local_executor` has a `void` callback/result shape and polls
  the local context once after the callback, rather than exposing a more general
  local-executor interface.

`ThreadExecutorTicker::tick()` and `try_tick()` are implemented and manually
poll the owner thread's `asio::io_context`.

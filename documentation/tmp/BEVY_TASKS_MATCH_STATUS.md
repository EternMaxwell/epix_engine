# Bevy Tasks — C++ Match Status

Reference: Bevy's `bevy_tasks` crate compared with the current `epix.task`
module under `epix_engine/task`.

This is an implementation-parity note, not the public API guide. See
[Task documentation](../task/quick.md) for current usage.

## Current mapping

| Bevy concept | Epix API | Status and differences |
| --- | --- | --- |
| `Task<T>` | `epix::task::Task<T>` | Implemented by `epix.async_task`; move-only, awaitable, detachable, cancellable, and synchronously observable through `block()` returning `std::expected`. |
| `TaskPoolBuilder` | `epix::task::TaskPoolBuilder` | Thread count and backend selection work. Thread naming and spawn/destroy callbacks are retained as no-op compatibility settings. |
| `TaskPool` | `epix::task::TaskPool` | Supports callables and zero/one-value stdexec senders. Backends are `StaticThreadPool` and `AsioThreadPool`; `ThreadPool` and `IoContext` remain aliases. |
| Local executor | `spawn_local`, `with_local_executor` | Implemented with a thread-local `asio::io_context`. The callback/result shape is narrower than Bevy's general local-executor API. |
| Scoped work | `scope<T>` | Implemented; waits for all tasks and preserves spawn order in the result vector. |
| Scoped external executor | `scope_with_executor<T>` | Present as a compatibility wrapper, but currently ignores the executor-selection arguments and delegates to `scope`. |
| Thread executor | `ThreadExecutor`, `ThreadExecutorTicker` | Implemented as an owner-thread `asio::io_context`; `tick()` and `try_tick()` manually poll queued work. |
| Global pools | `ComputeTaskPool`, `AsyncComputeTaskPool`, `IoTaskPool` | Implemented as independently initialized process-wide wrappers. |
| Parallel slices/iterators | `par_chunk_map`, `make_par` | Implemented on top of the compute pool. |
| Async channels | `epix.async_channel` | Implemented as a separate module and re-exported by `epix.task`. |
| Async broadcast | `epix.async_broadcast` | Implemented as a separate module and re-exported by `epix.task`. |

## Known gaps

The remaining compatibility gaps are kept in [the task TODO](../task/todo.md).
They concern builder callbacks, executor selection for scoped work, and the
shape of the local-executor helper—not missing core task execution.

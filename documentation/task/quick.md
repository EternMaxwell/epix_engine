# Task Module

`epix.task` provides stdexec-backed task pools, eagerly started task handles,
scoped parallel work, process-wide pool wrappers, a thread executor, and
range/slice parallel algorithms.

## Quick Guide

```cpp
import epix.task;

using namespace epix::task;

auto pool = TaskPoolBuilder{}
    .num_threads(4)
    .backend(TaskPoolBackend::StaticThreadPool)
    .build();

Task<int> task = pool.spawn([] { return 42; });
std::expected<int, std::exception_ptr> result = task.block();

auto coroutine = []() -> STDEXEC::task<int> {
    co_await STDEXEC::just();
    co_return 7;
};
Task<int> coroutine_task = pool.spawn(coroutine());

auto values = pool.scope<int>([](Scope<int>& scope) {
    scope.spawn([] { return 1; });
    scope.spawn([] { return 2; });
});
```

`TaskPool::spawn` and each global pool accept `STDEXEC::task<T>` coroutines in
addition to ordinary callables and senders. The returned `Task<T>` is itself
awaitable, so task-pool work composes directly inside another coroutine.

## References

- [`Task<T>`](task.md): blocking, awaiting, detaching, cancellation, and errors.
- [`TaskPool` and `TaskPoolBuilder`](task-pool.md): backends and spawning.
- [Global pools](global-pools.md): `ComputeTaskPool`, `AsyncComputeTaskPool`, and
  `IoTaskPool`.
- [Parallel ranges and slices](parallel-slice.md): `make_par`,
  `par_chunk_map`, and `par_splat_map`.
- [`ThreadExecutor`](thread-executor.md): thread-associated execution.
- [`epix.async_channel`](async-channel.md) and
  [`epix.async_broadcast`](async-broadcast.md): separate channel modules.

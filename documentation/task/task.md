# Task<T>

`Task<T>` is an eagerly started, move-only handle exported from `epix.task`.
It is implemented by the shared `epix.async_task` runtime and returned by
`TaskPool`, `ThreadExecutor`, and the global task pools.

## Blocking and Awaiting

```cpp
Task<int> task = pool.spawn([] { return 42; });
std::expected<int, std::exception_ptr> result = task.block();

if (!result) {
    std::rethrow_exception(result.error());
}
```

`Task<void>::block()` returns `std::expected<void, std::exception_ptr>`.
Blocking an empty or cancelled task returns an unexpected exception pointer.

Tasks are directly awaitable from a coroutine:

```cpp
STDEXEC::task<int> work(TaskPool& pool) {
    int first = co_await pool.spawn([] { return 20; });
    int second = co_await pool.spawn([] { return 22; });
    co_return first + second;
}
```

Awaiting rethrows a stored task exception and throws for an empty or cancelled
task.

This applies equally to tasks returned by a local `TaskPool`,
`ThreadExecutor`, `ComputeTaskPool`, `AsyncComputeTaskPool`, and `IoTaskPool`.
The pools also accept `STDEXEC::task<T>` coroutine objects directly; see
[TaskPool coroutines](task-pool.md#coroutines).

## Lifetime and Cancellation

- `is_finished()` is a non-blocking terminal-state check.
- `detach()` releases the handle while scheduled work may continue.
- Destroying a non-detached handle closes the task.
- `std::move(task).cancel()` closes the task and returns an awaiter. Await it to
  observe `optional<T>` (`bool` for `Task<void>`) after cancellation settles.
- `std::move(task).fallible()` converts to `FallibleTask<T>` when a coroutine
  should receive an expected-like result instead of throwing.

`operator bool()` indicates whether the handle still owns state; it is not a
completion check. Use `is_finished()` for that.

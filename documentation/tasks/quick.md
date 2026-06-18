# EPIX ENGINE TASKS MODULE

Thread-pool-based async task execution with awaitable `Task<T>` handles, scoped parallelism, global singleton pools, and MPMC/broadcast channels.

## Core Parts

- **[`Task<T>`](./task.md)**: eagerly-started awaitable handle returned by every `spawn` call; supports `block()`, `detach()`, `cancel()`, and `co_await` inside `asio::awaitable<T>`.
- **[`TaskPool`](./task-pool.md)**: multi-threaded work-stealing pool; primary entry point for `spawn`, coroutine-spawn, and `scope`-based parallel work.
- **[`TaskPoolBuilder`](./task-pool.md)**: fluent builder for `TaskPool` — configure thread count, name, spawn/destroy callbacks, and backend.
- **[`Scope<T>`](./task-pool.md)**: lexically-scoped parallel work — all spawned tasks are awaited before `scope()` returns.
- **[`available_parallelism()`](./task-pool.md)**: returns the logical CPU core count (minimum 1).
- **[`ComputeTaskPool` / `AsyncComputeTaskPool` / `IoTaskPool`](./global-pools.md)**: process-wide singleton pools for CPU, async-CPU, and IO work respectively.
- **[`ThreadExecutor`](./thread-executor.md)**: single-thread executor whose work must be ticked from its owning thread.
- **[`now_or_never()` / `check_ready()`](./task.md#non-blocking-poll)**: non-blocking poll — return the result if already done, else `nullopt`.
- **[`par_chunk_map` / `par_splat_map` / `par_chunk_map_mut` / `par_splat_map_mut`](./parallel-slice.md)**: parallel map over a slice using a `TaskPool`.
- **[`epix.async_channel`](./async-channel.md)**: MPMC async channel — `Sender<T>` / `Receiver<T>` with bounded and unbounded variants.
- **[`epix.async_broadcast`](./async-broadcast.md)**: broadcast channel where every active `Receiver<T>` sees every message.

## Quick Guide

```cpp
import epix.tasks;
using namespace epix::tasks;

// ── 1. Create a pool ──────────────────────────────────────────────────────────
TaskPool pool{4};  // 4 worker threads

// ── 2. Spawn a plain callable ─────────────────────────────────────────────────
Task<int> t = pool.spawn([]() { return 42; });

// Block the calling thread until complete:
std::optional<int> v = t.block();   // *v == 42

// ── 3. Spawn an asio coroutine ────────────────────────────────────────────────
auto coro = [&]() -> asio::awaitable<int> {
    int a = co_await pool.spawn([]() { return 3; });
    int b = co_await pool.spawn([]() { return 4; });
    co_return a + b;
};
auto result = pool.spawn(coro()).block();  // *result == 7

// ── 4. Scoped parallel work ───────────────────────────────────────────────────
std::vector<int> results = pool.scope<int>([](Scope<int>& s) {
    s.spawn([]() { return 1; });
    s.spawn([]() { return 2; });
    s.spawn([]() { return 3; });
});
// results contains {1, 2, 3} in completion order

// ── 5. Global singleton pool ──────────────────────────────────────────────────
ComputeTaskPool::get_or_init([]() { return TaskPool{available_parallelism()}; });
auto val = ComputeTaskPool::get().spawn([]() { return 99; }).block();
```

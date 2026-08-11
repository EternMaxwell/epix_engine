# Global Singleton Pools

Process-wide singleton `TaskPool` wrappers for the three conventional work categories.

## Overview

`ComputeTaskPool`, `AsyncComputeTaskPool`, and `IoTaskPool` are distinct types that each wrap a `TaskPool` behind a once-initialized global instance. The pattern matches `bevy_tasks` usages.

| Type | Intended use |
|------|-------------|
| `ComputeTaskPool` | CPU-bound, latency-sensitive compute |
| `AsyncComputeTaskPool` | CPU-bound work that can tolerate async scheduling |
| `IoTaskPool` | Blocking or async IO operations |

Each type is fully independent — they do not share a thread pool.

## Initialize

Each pool must be initialized once before use. Call `get_or_init` exactly once per pool type, typically at application startup:

```cpp
import epix.task;
using namespace epix::task;

// Initialize at startup
ComputeTaskPool::get_or_init([]() {
    return TaskPoolBuilder{}.num_threads(available_parallelism()).build();
});
AsyncComputeTaskPool::get_or_init([]() {
    return TaskPoolBuilder{}.num_threads(
        std::max<std::size_t>(1, available_parallelism() / 2)).build();
});
IoTaskPool::get_or_init([]() {
    return TaskPoolBuilder{}
        .num_threads(4)
        .backend(TaskPoolBackend::AsioThreadPool)
        .build();
});
```

## Access and Spawn

```cpp
// Get the singleton (terminates if not yet initialized)
auto& pool = ComputeTaskPool::get();
auto v = pool.spawn([]() { return 99; }).block();  // expected<int, exception_ptr>

// Coroutine work is supported by every global pool.
STDEXEC::task<std::vector<std::byte>> read_file_async();
Task<std::vector<std::byte>> read = IoTaskPool::get().spawn(read_file_async());

// Try-get: returns nullptr if not initialized
ComputeTaskPool* p = ComputeTaskPool::try_get();
if (p) { /* use *p */ }

// Scoped parallel work
auto results = ComputeTaskPool::get().scope<int>([](Scope<int>& s) {
    s.spawn([]() { return 1; });
    s.spawn([]() { return 2; });
});
```

## API per Pool Type

All three types expose the same interface:

| Method | Description |
|--------|-------------|
| `static T& get_or_init(fn)` | Initialize on first call; subsequent calls return the existing instance |
| `static T& get()` | Return the global instance; calls `std::terminate` if not initialized |
| `static T* try_get()` | Return pointer to instance or `nullptr` |
| `auto spawn(F&&)` | Delegate to inner `TaskPool::spawn` |
| `AsioExecutor get_asio_executor()` | Return the inner Asio executor; throws when the pool uses the static backend |
| `std::vector<T> scope<T>(Fn&&)` | Delegate to inner `TaskPool::scope` |
| `std::size_t thread_num()` | Return inner pool thread count |

## Constraints / Gotchas

- `spawn` supports callables, zero-or-one-value stdexec senders, and
  `STDEXEC::task<T>` coroutines, with the same behavior as `TaskPool::spawn`.
- A returned `Task<T>` may be blocked, detached, or `co_await`ed. Prefer
  `co_await` inside asynchronous code so a worker thread is not occupied by a
  blocking wait.

- Each pool is a separate global (`static once_flag` per type). Calling `get_or_init` after the first call is a no-op — the factory argument is ignored.
- `get()` calls `std::terminate` if called before `get_or_init`. Prefer `try_get()` to check initialization.
- The global instance is heap-allocated and never freed — this is intentional to avoid destruction-order issues at program exit.

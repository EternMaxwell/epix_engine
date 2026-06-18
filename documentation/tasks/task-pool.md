# TaskPool

A multi-threaded work pool for executing plain callables and `asio` coroutines.

## Overview

`TaskPool` owns a set of worker threads and dispatches work via an `asio::any_io_executor`. It is the primary way to obtain `Task<T>` handles in `epix.tasks`. The default backend is `asio::thread_pool`; an `asio::io_context` backend with manual thread management is also available and required when thread names or lifecycle callbacks are needed.

`available_parallelism()` returns `std::thread::hardware_concurrency()` clamped to a minimum of 1; use it as the default thread count.

## Create a Pool

```cpp
import epix.tasks;
using namespace epix::tasks;

// Default: hardware concurrency threads
TaskPool pool;

// Explicit thread count
TaskPool pool4{4};

// Explicit backend
TaskPool iop{4, TaskPoolBackend::IoContext};
```

## Spawn a Callable

```cpp
TaskPool pool{4};

// Non-void
Task<int> t = pool.spawn([]() { return 42; });
auto v = t.block();  // *v == 42

// Void
std::atomic<int> x{0};
pool.spawn([&]() { x.store(99); }).block();
```

## Spawn an asio Coroutine

```cpp
TaskPool pool{4};

// awaitable<T>
Task<int> t = pool.spawn([]() -> asio::awaitable<int> {
    co_return 10;
}());
auto v = t.block();  // *v == 10

// awaitable<void> — fire-and-forget
pool.spawn_detached([]() -> asio::awaitable<void> {
    co_return;
}());
```

## Scoped Parallel Work — `Scope<T>`

`scope<T>(fn)` runs `fn` synchronously, lets it spawn tasks via `Scope<T>::spawn`, and blocks until all spawned tasks complete before returning the collected results.

```cpp
TaskPool pool{4};

std::vector<int> results = pool.scope<int>([](Scope<int>& s) {
    s.spawn([]() { return 1; });
    s.spawn([]() { return 2; });
    s.spawn([]() { return 3; });
});
// results == {1, 2, 3} (order depends on scheduling)
```

`Scope<T>` also has `spawn_on_scope` (same as `spawn`) and `spawn_on_external` (currently delegates to `spawn`).

## TaskPoolBuilder

`TaskPoolBuilder` creates a `TaskPool` with a fluent API. Setting a thread name or lifecycle callbacks automatically selects the `IoContext` backend.

```cpp
std::atomic<int> spawned{0};
TaskPool pool = TaskPoolBuilder{}
    .num_threads(2)
    .thread_name("worker")
    .on_thread_spawn([&spawned]() { spawned.fetch_add(1); })
    .on_thread_destroy([]() { /* cleanup */ })
    .build();

EXPECT_EQ(pool.thread_num(), 2u);
```

| Method | Effect |
|--------|--------|
| `num_threads(n)` | Override thread count (default: `available_parallelism()`) |
| `stack_size(n)` | Stack size per thread (IoContext backend only) |
| `thread_name(name)` | OS thread name prefix; forces IoContext backend |
| `on_thread_spawn(f)` | Called on each worker thread after it starts; forces IoContext backend |
| `on_thread_destroy(f)` | Called on each worker thread before it exits; forces IoContext backend |
| `backend(b)` | Explicitly choose `ThreadPool` or `IoContext` |
| `build()` | Construct the `TaskPool` |

## Backend Selection

| Backend | Class | Drain on destroy | Thread naming |
|---------|-------|-----------------|---------------|
| `ThreadPool` (default) | `asio::thread_pool` | `pool.wait()` — drains all queued work | No |
| `IoContext` | `asio::io_context` + manual threads | work guard reset + `join` | Yes (`BS::this_thread::set_os_thread_name`) |

## Constraints / Gotchas

- `TaskPool` is move-only — copying is deleted.
- The pool's destructor blocks until all queued and in-flight work is complete (both backends drain before joining).
- `spawn_on_external` on `Scope<T>` currently delegates to `spawn` — there is no separate external executor.

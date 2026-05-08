# ThreadExecutor

A single-thread executor whose work must be ticked from the thread that created it.

## Overview

`ThreadExecutor` wraps an `asio::thread_pool{1}` and records the ID of the constructing thread. Work can be spawned from any thread, but `ticker()` only returns a `ThreadExecutorTicker` when called from the owner thread.

Matches `bevy_tasks::ThreadExecutor`.

## Usage

```cpp
import epix.tasks;
using namespace epix::tasks;

// Create on the thread that will own it
ThreadExecutor exec;

// Spawn from any thread
Task<int> t = exec.spawn([]() { return 123; });
auto v = t.block();  // *v == 123

// Void spawn
std::atomic<int> x{0};
exec.spawn([&]() { x.store(1); }).block();

// Exception propagation
auto bad = exec.spawn([]() -> int { throw std::runtime_error("fail"); });
// bad.block() rethrows
```

## ThreadExecutorTicker

`ticker()` returns a `ThreadExecutorTicker` only when called from the owning thread. The ticker's `try_tick()` is currently a stub — it always returns `false`.

```cpp
// On the owning thread:
ThreadExecutor exec;
auto ticker = exec.ticker();       // has_value() == true

// On any other thread:
std::thread([&]() {
    auto t = exec.ticker();        // has_value() == false
}).join();

if (ticker) {
    ticker->try_tick();            // always returns false (stub)
}
```

## `is_same`

```cpp
ThreadExecutor a, b;
a.is_same(a);  // true
a.is_same(b);  // false
```

## Constraints / Gotchas

- `ThreadExecutor` is non-copyable and non-movable.
- `try_tick()` is a stub — it does nothing and always returns `false`. See [todo.md](./todo.md).
- The internal pool uses one thread regardless of what the calling code does.

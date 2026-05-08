# Task\<T\>

An eagerly-started, move-only handle to a running asynchronous task.

## Overview

`Task<T>` is returned by every `spawn` call on `TaskPool` and `ThreadExecutor`. The work starts immediately at spawn time. The handle itself only tracks the result — dropping it (`detach()`) leaves the work running. `Task<void>` is a full specialization for fire-and-forget tasks.

`Task<T>` satisfies the asio `async_operation` concept: `co_await task` works natively inside any `asio::awaitable<T>` coroutine.

## Usage

### Blocking wait

```cpp
import epix.tasks;
using namespace epix::tasks;

TaskPool pool{2};
Task<int> t = pool.spawn([]() { return 42; });

std::optional<int> v = t.block();  // blocks calling thread
// *v == 42

// void variant: block() returns void
Task<void> tv = pool.spawn([]() { /* work */ });
tv.block();
```

### Non-blocking poll (`is_finished`)

```cpp
Task<int> t = pool.spawn([]() { return 1; });
if (t.is_finished()) {
    auto v = t.block();  // returns immediately
}
```

### Detach

```cpp
pool.spawn([]() { /* background work */ }).detach();
// handle is gone; work continues until pool is destroyed
```

### Cancel

```cpp
Task<int> t = pool.spawn([]() { /* long work */ return 0; });
std::optional<int> result = t.cancel();
// sets cancelled flag, blocks until done, returns value if completed before cancel
```

### co_await inside asio coroutines

```cpp
TaskPool pool{4};

auto coro = [&]() -> asio::awaitable<int> {
    int a = co_await pool.spawn([]() { return 3; });
    int b = co_await pool.spawn([]() { return 4; });
    co_return a + b;
};
auto v = pool.spawn(coro()).block();  // *v == 7
```

### Exception propagation

```cpp
auto t = pool.spawn([]() -> int { throw std::runtime_error("fail"); });
try {
    t.block();  // rethrows the stored exception
} catch (const std::runtime_error& e) { /* ... */ }
```

## Non-blocking Poll

`now_or_never` and `check_ready` (alias) return the result immediately if done, otherwise `nullopt` (or `false` for `void`):

```cpp
import epix.tasks;
using namespace epix::tasks;

TaskPool pool{1};
auto t = pool.spawn([]() { return 5; });
t.block();  // ensure complete

std::optional<int> v = now_or_never(t);   // returns 5
// equivalently:
v = check_ready(t);

// void variant:
auto tv = pool.spawn([]() {});
tv.block();
bool done = now_or_never(tv);   // true
```

## API Summary

| Method | Returns | Description |
|--------|---------|-------------|
| `is_finished()` | `bool` | Non-blocking: true if work is done or handle is null |
| `block()` | `optional<T>` / `void` | Block until done; rethrows stored exception |
| `detach()` | `void` | Drop the handle; work keeps running |
| `cancel()` | `optional<T>` / `void` | Signal cancellation, block until done, return value if available |
| `operator bool()` | `bool` | True while the task is still in-flight |
| `static make()` | `pair<Task, state_ptr>` | Create an unfulfilled Task and its shared state |

## Constraints / Gotchas

- `Task<T>` is move-only — copying is deleted.
- `co_await task` moves the task handle into the asio completion machinery. Do not use `task` after `co_await`.
- `block()` on a null (default-constructed or detached) `Task` returns `nullopt` immediately for `Task<T>` and is a no-op for `Task<void>`.
- Exception propagation through `co_await` inside a coroutine behaves the same as with `block()` — the exception is rethrown at the `co_await` site.
- `cancel()` only sets a flag visible to the task body via `TaskState::cancelled`; it does not interrupt running native code. The body must cooperate by checking the flag if it wants early termination.

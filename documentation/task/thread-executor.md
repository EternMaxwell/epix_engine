# ThreadExecutor

`ThreadExecutor` is a manually driven, single-thread executor. Work may be
spawned from any thread, but only the thread that created the executor can
obtain its ticker and run queued work.

## Usage

```cpp
import epix.task;

using namespace epix::task;

ThreadExecutor executor;
Task<int> task = executor.spawn([] { return 123; });

auto ticker = executor.ticker();
if (ticker) {
    while (!task.is_finished()) {
        ticker->tick();
    }
}

auto result = task.block();
if (!result) {
    std::rethrow_exception(result.error());
}
```

Do not call `block()` before driving the executor: the queued work cannot finish
until the owning thread ticks it.

## Ticker

`ticker()` returns `std::optional<ThreadExecutorTicker>`. It contains a ticker
on the owner thread and returns `std::nullopt` on every other thread.

- `tick()` polls one queued task.
- `try_tick()` also polls one task and reports whether any work ran.

Both methods use the executor's `asio::io_context`; the executor does not create
a worker thread of its own.

## Identity and lifetime

`is_same(other)` is true only when both references identify the same executor.
`ThreadExecutor` is non-copyable and non-movable, so it must remain alive while
tasks can still be scheduled against it.

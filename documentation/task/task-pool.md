# TaskPool

`TaskPool` runs callables and stdexec senders on either a static stdexec thread
pool or an Asio stdexec thread pool.

## Construction

```cpp
import epix.task;
using namespace epix::task;

TaskPool defaults; // available_parallelism() worker threads

auto cpu_pool = TaskPoolBuilder{}
    .num_threads(4)
    .backend(TaskPoolBackend::StaticThreadPool)
    .build();

auto io_pool = TaskPoolBuilder{}
    .num_threads(2)
    .backend(TaskPoolBackend::AsioThreadPool)
    .build();
```

The compatibility enumerators `ThreadPool` and `IoContext` alias
`StaticThreadPool` and `AsioThreadPool` respectively.

`thread_name`, `on_thread_spawn`, and `on_thread_destroy` remain accepted by
the builder for source compatibility, but are no-ops with the current stdexec
backends. Use `backend(...)` to select a backend explicitly.

## Spawning

```cpp
Task<int> callable = cpu_pool.spawn([] { return 42; });
auto result = callable.block(); // expected<int, exception_ptr>

auto sender = STDEXEC::just(21) | STDEXEC::then([](int x) { return x * 2; });
Task<int> sender_task = cpu_pool.spawn(std::move(sender));
```

### Coroutines

`spawn` accepts an already-created `STDEXEC::task<T>` coroutine and schedules
its first continuation on the selected pool backend. The result is flattened to
`Task<T>` rather than `Task<STDEXEC::task<T>>`.

```cpp
STDEXEC::task<std::string> read_name() {
    auto bytes = co_await read_bytes();
    co_return decode_name(bytes);
}

Task<std::string> name = io_pool.spawn(read_name());
```

Because `Task<T>` is awaitable, a coroutine can spawn work on the pool and wait
without blocking its current thread:

```cpp
STDEXEC::task<int> combine(TaskPool& pool) {
    int a = co_await pool.spawn([] { return expensive_part_a(); });
    int b = co_await pool.spawn([] { return expensive_part_b(); });
    co_return a + b;
}
```

Pass the coroutine object itself (`pool.spawn(read_name())`). A callable that
merely returns `STDEXEC::task<T>` selects the callable overload and does not
provide the same sender-flattening contract.

`spawn_local` queues work on a thread-local `asio::io_context`.
`with_local_executor(...)` invokes its callback and then polls that local
context, providing the public way to drive the queued local work.
`try_get_asio_executor()` returns an executor only for the Asio backend;
`get_asio_executor()` throws for the static backend.

## Scoped Work

```cpp
std::vector<int> values = cpu_pool.scope<int>([](Scope<int>& scope) {
    scope.spawn([] { return 1; });
    scope.spawn([] { return 2; });
    scope.spawn([] { return 3; });
});
```

`scope` waits for every spawned task and returns results in spawn order.
`scope_with_executor(...)` is currently a compatibility wrapper: its executor
arguments are ignored and it delegates to `scope`.

`TaskPool` is movable and non-copyable. Keep the pool alive while task work is
running; detached work may be dropped when its scheduling backend disappears.

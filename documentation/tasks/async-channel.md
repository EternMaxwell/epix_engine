# Async Channel (`epix.async_channel`)

MPMC (multi-producer, multi-consumer) channel with blocking and async send/receive.

## Overview

`epix.async_channel` is a separate module (not re-exported by `epix.tasks`). It provides:

- `Sender<T>` / `Receiver<T>` — strong reference-counted handles
- `WeakSender<T>` / `WeakReceiver<T>` — weak handles that do not keep the channel alive
- `unbounded<T>()` / `bounded<T>(cap)` — factory functions

The channel closes automatically when all senders or all receivers are destroyed. Attempting to send to a channel with no receivers (or a closed channel) returns an error.

## Create a Channel

```cpp
import epix.async_channel;
using namespace epix::async_channel;

// Unbounded (no capacity limit)
auto [sender, receiver] = unbounded<int>();

// Bounded (blocks senders when full)
auto [tx, rx] = bounded<int>(16);
```

## Send

```cpp
// Try (non-blocking) — returns TrySendError on failure
auto res = sender.try_send(42);
if (!res) {
    if (res.error().is_full()) { /* ... */ }
    if (res.error().is_closed()) { /* ... */ }
}

// Async (inside asio coroutine) — waits until space available
auto coro = [&]() -> asio::awaitable<void> {
    auto r = co_await sender.send(42);
    if (!r) { /* SendError: channel closed */ }
};

// Blocking
auto r = sender.send_blocking(42);
```

## Receive

```cpp
// Try (non-blocking) — returns TryRecvError on failure
auto val = receiver.try_recv();
if (val) {
    use(*val);
} else if (val.error() == TryRecvError::Empty) { /* no message yet */ }

// Async (inside asio coroutine)
auto coro = [&]() -> asio::awaitable<void> {
    auto r = co_await receiver.recv();
    if (r) { use(*r); }
    else { /* RecvError: closed */ }
};

// Blocking
auto r = receiver.recv_blocking();
```

## Weak Handles

```cpp
WeakSender<int> ws = sender.downgrade();
// Upgrading returns nullopt if channel is closed
if (auto s = ws.upgrade()) {
    s->try_send(1);
}

WeakReceiver<int> wr = receiver.downgrade();
if (auto r = wr.upgrade()) {
    r->try_recv();
}
```

## Channel Lifetime & Closing

The channel is alive as long as at least one `Sender` AND at least one `Receiver` exist. Destroying all senders closes the channel from the sending side (receivers drain then get `Closed` errors). Calling `close()` on any handle closes the channel immediately.

```cpp
sender.close();              // explicit close
bool already = sender.is_closed();
std::size_t n = sender.len();
auto cap = sender.capacity();  // nullopt for unbounded
```

## Error Types

| Type | Members | Meaning |
|------|---------|---------|
| `TrySendError<T>` | `kind` (`Full` / `Closed`), `msg` | Non-blocking send failed |
| `SendError<T>` | `msg` | Async/blocking send: channel closed |
| `TryRecvError` | `Empty` / `Closed` | Non-blocking receive failed |
| `RecvError` | (empty) | Async/blocking receive: channel closed |

## Constraints / Gotchas

- `Sender<T>` and `Receiver<T>` are both copyable (each copy increments a reference count). `WeakSender` / `WeakReceiver` are also copyable.
- The async `send` and `recv` methods use `asio::post` spin-polling instead of a condition variable — they must run inside an asio executor context.
- Sending to a bounded channel with no receivers returns `TrySendError::Closed`, not `Full`.

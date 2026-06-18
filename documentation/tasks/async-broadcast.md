# Async Broadcast (`epix.async_broadcast`)

Broadcast channel where every active `Receiver<T>` sees every message sent after it was created.

## Overview

`epix.async_broadcast` is a separate module (not re-exported by `epix.tasks`). Unlike `epix.async_channel`'s MPMC queue where each message is consumed by one receiver, a broadcast channel delivers each message to all active receivers independently.

Key types:
- `Sender<T>` — sends messages to all active receivers
- `Receiver<T>` — active receiver; counts as a backpressure participant
- `InactiveReceiver<T>` — parked receiver that does not contribute to backpressure; reactivate with `activate()`

## Create a Channel

```cpp
import epix.async_broadcast;
using namespace epix::async_broadcast;

// Capacity: max messages buffered before senders block
auto [sender, receiver] = broadcast<int>(16);
```

## Send

```cpp
// Try (non-blocking)
auto res = sender.try_broadcast(42);
if (!res) {
    if (res.error().is_full()) { /* ... */ }
    if (res.error().is_closed()) { /* ... */ }
    if (res.error().is_inactive()) { /* no active receivers */ }
}

// Async (inside asio coroutine)
auto coro = [&]() -> asio::awaitable<void> {
    auto r = co_await sender.broadcast(42);
    if (!r) { /* SendError: closed */ }
};
```

## Receive

```cpp
// Try (non-blocking)
auto val = receiver.try_recv();
if (val) { use(*val); }

// Async (inside asio coroutine)
auto coro = [&]() -> asio::awaitable<void> {
    auto r = co_await receiver.recv();
    if (r) { use(*r); }
    else { /* RecvError::Closed */ }
};
```

## Overflow Mode

When overflow is enabled, sending to a full channel drops the oldest message instead of blocking:

```cpp
sender.set_overflow(true);
sender.try_broadcast(99);  // never returns Full
```

## Multiple Receivers

```cpp
auto [sender, rx1] = broadcast<int>(8);
Receiver<int> rx2 = sender.new_receiver();  // starts at current tail
// Both rx1 and rx2 will receive subsequent messages independently
```

## InactiveReceiver

An `InactiveReceiver` does not consume buffer space or increase the active receiver count (so senders do not block waiting for it to drain). Reactivate it to resume receiving from the current position.

```cpp
InactiveReceiver<int> parked = receiver.deactivate();
// ... later ...
Receiver<int> active = parked.activate();
// active starts reading from where it left off
```

## Error Types

| Type | Members | Meaning |
|------|---------|---------|
| `TrySendError<T>` | `kind` (`Full` / `Closed` / `Inactive`), `msg` | Non-blocking broadcast failed |
| `SendError<T>` | `msg` | Async send: channel closed |
| `TryRecvError` | `Empty` / `Closed` | Non-blocking receive failed |
| `RecvError` | `Closed` | Async receive: channel closed |

## Constraints / Gotchas

- Each `Receiver<T>` tracks its own read cursor — messages are not removed from the buffer until all active receivers have read them (or overflow evicts them).
- `Sender<T>::new_receiver()` creates a receiver starting at the current tail — it does not see past messages.
- The async `broadcast` and `recv` methods use `asio::post` spin-polling; run inside an asio executor context.
- A `TrySendError::Inactive` means there are no active receivers. Messages sent in this state are dropped.

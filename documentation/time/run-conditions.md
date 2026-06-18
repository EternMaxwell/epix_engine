# Time-Based Run Conditions

Factory functions that return ECS run conditions based on virtual or real time. Pass the result of these functions to `.run_if()` on a system set.

## Overview

All functions in this file return lambdas that capture an internal [`Timer`](./stopwatch-timer.md#timer) and read the appropriate `Time<T>` resource each frame. They are composable with other run conditions using `.and_()` / `.or_()`.

---

## Periodic Conditions

### `on_timer(duration)` — virtual time

Returns a run condition that fires every `duration` of virtual (`Time<>`) game time. Respects pause and speed changes.

```cpp
using namespace std::chrono_literals;

// Run my_system every 500 ms of game time:
app.add_systems(
    core::Update,
    core::into(my_system)
        .run_if(time::on_timer(500ms)));
```

### `on_real_timer(duration)` — real time

Same as `on_timer` but driven by `Time<Real>`. Fires on wall-clock intervals regardless of pause or speed.

```cpp
// Send a heartbeat every 1 real second, even when game is paused:
app.add_systems(
    core::Update,
    core::into(send_heartbeat)
        .run_if(time::on_real_timer(std::chrono::seconds(1))));
```

---

## One-Shot Delay Conditions

### `once_after_delay(duration)` — virtual time

Returns a run condition that returns `false` until `duration` of virtual time has elapsed, then returns `true` for exactly one frame.

```cpp
// Trigger an intro cutscene exactly 3 game-seconds after startup:
app.add_systems(
    core::Update,
    core::into(play_intro)
        .run_if(time::once_after_delay(std::chrono::seconds(3))));
```

### `once_after_real_delay(duration)` — real time

Same as `once_after_delay` but based on `Time<Real>`.

---

## Gated Conditions

### `repeating_after_delay(duration)` — virtual time

Returns a run condition that returns `false` for the first `duration` of virtual time, then returns `true` every frame thereafter. Useful for delaying the start of a recurring system.

```cpp
// Skip the first 2 seconds, then run every frame:
app.add_systems(
    core::Update,
    core::into(my_system)
        .run_if(time::repeating_after_delay(std::chrono::seconds(2))));
```

### `repeating_after_real_delay(duration)` — real time

Same as `repeating_after_delay` but based on `Time<Real>`.

---

## `paused` — run condition

A plain function (not a factory) that returns `true` when `Time<Virtual>` is paused. Use it to run systems only while the game is paused (e.g. a pause-menu system).

```cpp
app.add_systems(
    core::Update,
    core::into(render_pause_menu)
        .run_if(time::paused));
```

---

## Constraints

- Each call to `on_timer(d)` / `on_real_timer(d)` / etc. creates an **independent** timer. Passing the same duration to two calls gives two separate timers. Store the result in a variable if you want the same timer shared across multiple system sets (not currently possible via run-condition lambda capture; consider using a `Timer` component/resource instead).
- The timer inside a run condition is part of its lambda closure — it is not a resource and cannot be inspected or reset from outside.
- `TimePlugin` must be added before these run conditions are evaluated (the lambdas read `Time<>` / `Time<Real>` resources).

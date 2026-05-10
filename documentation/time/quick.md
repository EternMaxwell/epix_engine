# EPIX ENGINE TIME MODULE

Provides frame-delta time, wall-clock time, virtual (scalable/pausable) time, fixed-timestep scheduling, and per-frame timers — all wired into the ECS via a single plugin.

## Core Parts

- **[`TimePlugin`](./time-clock.md#timeplugin)**: registers all four `Time<T>` resources, `TimeUpdateConfig`, and the `FixedMain` schedule with its sub-schedules.
- **[`Time<>`](./time-clock.md)**: generic clock resource updated each frame with virtual-time delta; the primary delta-time source for `Update` systems.
- **[`Time<Real>`](./time-clock.md#timereal)**: wall-clock resource driven by `steady_clock`; unaffected by pause or speed changes.
- **[`Time<Virtual>`](./time-clock.md#timevirtual)**: virtual (game) time derived from real time; can be paused, sped up, or slowed down.
- **[`Time<Fixed>`](./fixed-timestep.md)**: fixed-timestep resource whose `expend()` loop drives `FixedMain`.
- **[`FixedMain` / `FixedFirst` / `FixedPreUpdate` / `FixedUpdate` / `FixedPostUpdate` / `FixedLast`](./fixed-timestep.md#schedules)**: schedule labels for fixed-timestep work.
- **[`TimeUpdateConfig`](./time-clock.md#timeupdateconfig)**: resource for controlling how real time is advanced (automatic, manual instant/duration, or fixed-timestep driven).
- **[`Stopwatch`](./stopwatch-timer.md)**: pausable elapsed-time accumulator driven by explicit `tick()` calls.
- **[`Timer`](./stopwatch-timer.md#timer)** / **[`TimerMode`](./stopwatch-timer.md#timermode)**: countdown / repeating timer with `just_finished()` and `times_finished_this_tick()`.
- **[`on_timer`](./run-conditions.md)** / **[`on_real_timer`](./run-conditions.md)**: run-condition factories that fire a system periodically.
- **[`once_after_delay`](./run-conditions.md)** / **[`once_after_real_delay`](./run-conditions.md)**: run conditions that fire once after a delay.
- **[`repeating_after_delay`](./run-conditions.md)** / **[`repeating_after_real_delay`](./run-conditions.md)**: run conditions that become permanently true after a delay.
- **[`paused`](./run-conditions.md)**: run condition that returns true while virtual time is paused.

## Quick Guide

```cpp
import epix.core;
import epix.time;

using namespace epix;

// 1. Add the plugin.
app.add_plugins(time::TimePlugin{});

// 2. Read frame delta in an Update system via Time<> (virtual time).
void update_movement(
    core::Res<time::Time<>> time,
    core::Query<core::Item<transform::Transform&>> transforms)
{
    float dt = time->delta_secs();
    for (auto&& [tf] : transforms.iter()) {
        tf.translation.x += 100.0f * dt;
    }
}
app.add_systems(core::Update, core::into(update_movement));

// 3. Run physics at a fixed rate (default 64 Hz) in FixedUpdate.
void physics_step(core::Res<time::Time<Fixed>> fixed_time) {
    float dt = fixed_time->delta_secs(); // always == timestep (e.g. ~15.625 ms)
    // integrate physics ...
}
app.add_systems(time::FixedUpdate, core::into(physics_step));

// 4. Pause game time (Time<> and Time<Virtual> stop advancing).
void toggle_pause(
    core::Res<time::ButtonInput<time::KeyCode>> keys,
    core::ResMut<time::Time<Virtual>> virt)
{
    if (keys->just_pressed(input::KeyCode::KeyEscape)) {
        virt->toggle();
    }
}

// 5. Schedule a system to run every 5 seconds of virtual time.
app.add_systems(
    core::Update,
    core::into(my_periodic_system)
        .run_if(time::on_timer(std::chrono::seconds(5))));
```

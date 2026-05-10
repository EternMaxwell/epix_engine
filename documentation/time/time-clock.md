# Time&lt;T&gt; — Core Clock

The foundational clock template that tracks per-frame delta time, total elapsed time, and wrapped elapsed time.

## Overview

`Time<T>` is parameterised by a context tag:

| Instantiation | Context | Role |
|---------------|---------|------|
| `Time<>` (= `Time<GenericTag>`) | `GenericTag` | Current active time; copied from virtual time each frame. The primary delta-time resource. |
| `Time<Real>` | `Real` | Wall-clock time from `steady_clock`. Never paused or scaled. |
| `Time<Virtual>` | `Virtual` | Game time. Derived from real time; can be paused, sped up, or slowed. |
| `Time<Fixed>` | `Fixed` | Fixed-timestep clock. See [fixed-timestep.md](./fixed-timestep.md). |

All specializations expose the same read accessors inherited from the base `Time<GenericTag>`.

---

## Common Read API

Every `Time<T>` variant exposes:

```cpp
// Delta (duration of the last advance)
std::chrono::nanoseconds delta()         const noexcept;
float                    delta_secs()    const noexcept;  // float seconds
double                   delta_secs_f64() const noexcept; // double seconds

// Total elapsed since the first advance
std::chrono::nanoseconds elapsed()         const noexcept;
float                    elapsed_secs()    const noexcept;
double                   elapsed_secs_f64() const noexcept;

// Elapsed modulo wrap_period (default 1 hour)
std::chrono::nanoseconds elapsed_wrapped()             const noexcept;
float                    elapsed_secs_wrapped()         const noexcept;
double                   elapsed_secs_wrapped_f64()     const noexcept;

// Wrap period
std::chrono::nanoseconds wrap_period() const noexcept;
void set_wrap_period(std::chrono::nanoseconds period)  noexcept;  // must be != 0
```

### Usage — reading delta time in a system

```cpp
// From epix_engine/experimental/examples/voxel_path_tracer.cpp
void camera_control(
    core::Res<time::Time<>> game_time,
    /* ... */)
{
    const float dt = game_time->delta_secs();
    // move camera by speed * dt
}
```

---

## `Time<Real>`

Wall-clock time driven by `std::chrono::steady_clock`. Initialized and updated automatically by `TimePlugin` in the `First` schedule.

Additional API:

```cpp
// Access the startup / first-update / last-update instants:
std::chrono::steady_clock::time_point startup() const noexcept;
std::optional<std::chrono::steady_clock::time_point> first_update() const noexcept;
std::optional<std::chrono::steady_clock::time_point> last_update()  const noexcept;

// Manual update methods (used by TimeUpdateConfig):
void update() noexcept;                                                 // uses steady_clock::now()
void update_with_instant(std::chrono::steady_clock::time_point) noexcept;
void update_with_duration(std::chrono::nanoseconds) noexcept;

Time<> as_generic() const noexcept;   // copy timing fields to Time<GenericTag>
```

`Time<Real>` is unaffected by `Time<Virtual>` speed or pause state. Use it for effects that must track wall time (loading screens, real-time network timeouts, etc.).

---

## `Time<Virtual>`

Game time derived from real time. `TimePlugin` feeds the real delta through `advance_with_raw_delta()` each `First` tick, which clamps it to `max_delta` (default 250 ms) then scales by `relative_speed`.

Additional API:

```cpp
// Speed control
float  relative_speed()    const noexcept;  // user-set multiplier
double relative_speed_f64() const noexcept;
float  effective_speed()    const noexcept;  // accounting for pause
double effective_speed_f64() const noexcept;
void set_relative_speed(float ratio) noexcept;      // must be >= 0 and finite
void set_relative_speed_f64(double ratio) noexcept;

// Pause control
void pause()     noexcept;
void unpause()   noexcept;
void toggle()    noexcept;
bool is_paused() const noexcept;   // currently paused
bool was_paused() const noexcept;  // was paused during the previous advance

// Max delta clamp
std::chrono::nanoseconds max_delta() const noexcept;
void set_max_delta(std::chrono::nanoseconds) noexcept;  // must be != 0
// Factory:
static Time from_max_delta(std::chrono::nanoseconds) noexcept;

Time<> as_generic() const noexcept;
```

The `max_delta` clamp prevents the "spiral of death" — if a frame takes longer than `max_delta`, virtual time advances by at most `max_delta` rather than the full real delta.

### Usage — slow-motion and pause

```cpp
void toggle_slowmo(
    core::ResMut<time::Time<Virtual>> virt)
{
    if (virt->relative_speed_f64() == 1.0) {
        virt->set_relative_speed(0.25f); // 4× slow-motion
    } else {
        virt->set_relative_speed(1.0f);
    }
}
```

---

## `TimeUpdateConfig`

Resource that controls how `Time<Real>` is advanced each frame. Set its fields before `app.run()` or in a startup system.

```cpp
enum class TimeUpdateStrategy {
    Automatic,       // default: use steady_clock::now() each frame
    ManualInstant,   // advance to manual_instant
    ManualDuration,  // advance by manual_duration each frame
    FixedTimesteps,  // advance by fixed_time.timestep() * fixed_timestep_factor
};

struct TimeUpdateConfig {
    TimeUpdateStrategy strategy = TimeUpdateStrategy::Automatic;
    std::chrono::steady_clock::time_point manual_instant{};
    std::chrono::nanoseconds manual_duration{0};
    std::uint32_t fixed_timestep_factor = 1;
};
```

`ManualDuration` and `FixedTimesteps` strategies are useful for deterministic testing and replay.

> ⚠ `TimeUpdateConfig` must be modified before the `First` schedule runs in a given frame — it is consumed at the start of each frame.

---

## `TimePlugin`

Registers all time resources and the fixed-timestep schedule infrastructure into the `App`.

**What `build()` does:**
1. `init_resource` for `Time<>`, `Time<Real>`, `Time<Virtual>`, `Time<Fixed>`, and `TimeUpdateConfig`.
2. Adds the real-time update system to the `First` schedule, which reads `TimeUpdateConfig` and advances all four clocks.
3. Registers `FixedFirst`, `FixedPreUpdate`, `FixedUpdate`, `FixedPostUpdate`, and `FixedLast` schedules.
4. Registers `FixedMain` with a loop condition (`Time<Fixed>::expend()`) and inserts it into the schedule order after `StateTransition` (before `Update`).

```cpp
app.add_plugins(time::TimePlugin{});
```

`TimePlugin` must be added exactly once. All other time functionality depends on it.

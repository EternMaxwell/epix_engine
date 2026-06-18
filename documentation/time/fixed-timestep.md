# Fixed-Timestep Time

`Time<Fixed>` and the `FixedMain` schedule family provide deterministic fixed-rate system execution independent of frame rate.

## Overview

The fixed-timestep system works via an accumulator pattern:

1. Each frame, the virtual-time delta is added to `Time<Fixed>`'s `overstep` accumulator.
2. `FixedMain` loops: while `expend()` returns `true` (overstep ≥ timestep), it subtracts one timestep from overstep, advances `Time<Fixed>` by one step, and runs the sub-schedules.
3. After the loop, `Time<>` is restored to virtual-time values.

Systems in `FixedUpdate` always see `Time<Fixed>::delta_secs()` equal to the configured timestep (e.g. `~0.015625 s` at the default 64 Hz), regardless of real frame duration.

The default timestep is 64 Hz (~15.625 ms, = `std::chrono::microseconds(15625)`).

---

## `Time<Fixed>`

### Factory constructors

```cpp
Time<Fixed> t = Time<Fixed>::from_hz(60.0);          // 60 Hz
Time<Fixed> t = Time<Fixed>::from_seconds(1.0 / 30); // 30 Hz
Time<Fixed> t = Time<Fixed>::from_duration(std::chrono::milliseconds(16));
```

These factories are for direct construction. The `Time<Fixed>` resource is owned by `TimePlugin` — to change the timestep at runtime, use `ResMut<time::Time<Fixed>>`:

```cpp
void startup(core::ResMut<time::Time<Fixed>> fixed) {
    fixed->set_timestep_hz(30.0);  // switch to 30 Hz
}
```

### API

```cpp
// Timestep configuration
std::chrono::nanoseconds timestep() const noexcept;
void set_timestep(std::chrono::nanoseconds) noexcept;       // must be != 0
void set_timestep_seconds(double seconds)   noexcept;       // must be > 0 and finite
void set_timestep_hz(double hz)             noexcept;       // must be > 0 and finite

// Overstep (accumulated unconsumed real time)
std::chrono::nanoseconds overstep()                   const noexcept;
float                    overstep_fraction()           const noexcept;  // overstep / timestep
double                   overstep_fraction_f64()       const noexcept;
void accumulate_overstep(std::chrono::nanoseconds)           noexcept;
void discard_overstep(std::chrono::nanoseconds)              noexcept;

// Fixed-loop control
bool expend() noexcept;  // subtract one timestep; advance clock; return true if overstep was enough

// Inherited from Time<>: delta(), delta_secs(), elapsed(), elapsed_secs(), etc.
```

### Usage in a FixedUpdate system

```cpp
// From epix_engine/extension/fallingsand/src/systems.cpp
app.add_systems(time::FixedUpdate,
    core::into(simulate_worlds).set_name("fallingsand simulate"));

// The system receives Time<Fixed> (or Time<>, which is overwritten to fixed values
// during FixedMain):
void simulate_worlds(core::Res<time::Time<Fixed>> fixed_time) {
    float dt = fixed_time->delta_secs();  // == 1/64 s at default rate
    // deterministic simulation step
}
```

---

## Schedules

`TimePlugin` registers these schedule labels. Add systems to them exactly as you would to `Update`:

| Schedule | Order within `FixedMain` |
|----------|--------------------------|
| `FixedFirst` | First |
| `FixedPreUpdate` | Before `FixedUpdate` |
| `FixedUpdate` | Main fixed-rate work |
| `FixedPostUpdate` | After `FixedUpdate` |
| `FixedLast` | Last |

`FixedMain` itself runs after `StateTransition` and before `Update` in the top-level schedule order.

```cpp
app.add_systems(time::FixedPreUpdate, core::into(accumulate_physics));
app.add_systems(time::FixedUpdate,    core::into(step_physics));
app.add_systems(time::FixedPostUpdate, core::into(sync_transforms));
```

---

## `overstep_fraction`

Use `overstep_fraction()` for render interpolation — it represents how far into the current fixed step the renderer is drawing:

```cpp
void render_interpolated(
    core::Res<time::Time<Fixed>> fixed,
    /* query transforms */ ...)
{
    float alpha = fixed->overstep_fraction();
    // lerp between previous and current position by alpha
}
```

---

## Constraints

- `FixedMain` only runs if `Time<Fixed>::expend()` returns `true`. If frames are very fast, it may not run at all in some frames; if frames are very slow, it runs multiple times to catch up.
- During `FixedMain`, `Time<>` is overwritten with fixed-time values. It is restored to virtual-time values after the loop. Do not cache `Time<>` values across a fixed-loop iteration boundary.
- Changing the timestep mid-run takes effect on the next `FixedMain` loop evaluation.

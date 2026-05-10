# Stopwatch and Timer

Utility types for measuring and counting elapsed time within game systems. Both are driven by explicit `tick()` calls — they do not automatically advance.

---

## `Stopwatch`

A pausable accumulator of elapsed time. Suitable for measuring how long a process has been running.

### Usage

```cpp
using namespace epix::time;
using namespace std::chrono_literals;

Stopwatch sw;
sw.tick(250ms);    // advance by 250 ms
sw.elapsed_secs(); // → 0.25f
sw.pause();
sw.tick(1s);       // no effect while paused
sw.elapsed_secs(); // still 0.25f
sw.unpause();
sw.tick(500ms);
sw.elapsed_secs(); // → 0.75f
sw.reset();        // elapsed back to 0; pause state preserved
```

### API

```cpp
// Read
std::chrono::nanoseconds elapsed()         const noexcept;
float                    elapsed_secs()    const noexcept;
double                   elapsed_secs_f64() const noexcept;

// Advance
Stopwatch& tick(std::chrono::nanoseconds delta) noexcept;  // no-op if paused

// Pause
void pause()     noexcept;
void unpause()   noexcept;
bool is_paused() const noexcept;

// Reset
void reset()                                noexcept;  // sets elapsed = 0
void set_elapsed(std::chrono::nanoseconds)  noexcept;  // set directly
```

### Constraints

- `reset()` does **not** clear the pause state. A paused stopwatch stays paused after reset.
- `tick()` must be called manually each frame; the stopwatch does not auto-advance.

---

## `TimerMode`

```cpp
enum class TimerMode {
    Once,      // fires once; stops after finishing
    Repeating, // resets and re-fires automatically
};
```

---

## `Timer`

A countdown timer built on a `Stopwatch`. After `tick()`-ing past the `duration`, `just_finished()` returns `true` for exactly one frame. In `Repeating` mode the timer wraps and `times_finished_this_tick()` counts how many full periods passed in a single tick.

### Construct

```cpp
// Preferred factory:
auto t = Timer::from_seconds(2.5f, TimerMode::Once);
// Or via constructor:
auto t = Timer(std::chrono::milliseconds(500), TimerMode::Repeating);
```

### Usage in a system (from tests/timer_tests.cpp pattern)

```cpp
// Component-stored timer that fires once after 10 s:
struct Explodable { time::Timer fuse; };

// In startup:
world.spawn(Explodable{ .fuse = time::Timer::from_seconds(10.0f, time::TimerMode::Once) });

// In Update system:
void tick_fuses(
    core::Res<time::Time<>> time,
    core::Query<core::Item<Explodable&>> explodables,
    core::Commands cmd)
{
    std::chrono::nanoseconds dt = time->delta();
    for (auto&& [e_id, expl] : explodables.iter_with_entity()) {
        expl.fuse.tick(dt);
        if (expl.fuse.just_finished()) {
            // detonate
            cmd.entity(e_id).despawn();
        }
    }
}
```

### API

```cpp
// Factory
static Timer from_seconds(float duration, TimerMode mode) noexcept;

// Advance
Timer& tick(std::chrono::nanoseconds delta) noexcept;

// State
bool just_finished()   const noexcept;  // true only on the tick it finished
bool is_finished()     const noexcept;  // true from completion onward (Once) or current cycle (Repeating)
std::uint32_t times_finished_this_tick() const noexcept;  // 0, 1, or > 1 for Repeating with small duration

// Progress
float elapsed_secs()        const noexcept;
double elapsed_secs_f64()   const noexcept;
std::chrono::nanoseconds elapsed()  const noexcept;
std::chrono::nanoseconds remaining() const noexcept;
float remaining_secs()      const noexcept;
float fraction()            const noexcept;  // [0, 1]
float fraction_remaining()  const noexcept;  // 1 - fraction()

// Duration
std::chrono::nanoseconds duration() const noexcept;
void set_duration(std::chrono::nanoseconds)   noexcept;
void set_elapsed(std::chrono::nanoseconds)    noexcept;

// Mode
TimerMode mode() const noexcept;
void set_mode(TimerMode) noexcept;

// Shortcuts
void finish()        noexcept;  // advance to exactly finished
void almost_finish() noexcept;  // advance to 1 ns before finished

// Pause
void pause()     noexcept;
void unpause()   noexcept;
bool is_paused() const noexcept;

// Reset
void reset() noexcept;  // elapsed = 0, finished = false
```

### Constraints

- `just_finished()` is `true` for exactly one `tick()` call. Check it in the same frame you tick.
- For a `Once` timer: after `is_finished()` is `true`, further `tick()` calls are no-ops. Call `reset()` to reuse.
- For a `Repeating` timer: `is_finished()` is `true` only during the tick that completed the cycle (not on subsequent ticks); `elapsed()` wraps around to the remainder.
- `set_mode(Repeating)` on a finished `Once` timer resets the stopwatch automatically.
- The timer does not read from any ECS resource — you must supply the delta yourself (usually `time->delta()`).

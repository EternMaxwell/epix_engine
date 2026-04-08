# Change Detection

Track when components or resources were added or modified: `Tick`, `Ref<T>`, `RefMut<T>`, and the `copy_ref<T>` trait.

## Overview

The engine maintains two ticks per component or resource slot: `added` and `modified`. Every system has a `last_run` tick. The `is_added()` / `is_modified()` predicates compare those ticks to tell you whether data changed since the system last ran.

Change detection is based on the `Tick` type, which uses wrapping 32-bit arithmetic. The engine automatically clamps ticks that become too old (`check_change_tick()`).

## Usage

### Change detection in queries — `Ref<T>` and `RefMut<T>`

Instead of `const T&` or `T&`, use `Ref<T>` or `RefMut<T>` to get tick metadata alongside the value:

```cpp
void detect_moved(Query<Item<Entity, Ref<Position>>> query) {
    for (auto&& [e, pos] : query.iter()) {
        if (pos.is_added())    std::println("Entity {} just spawned",  e.index);
        if (pos.is_modified()) std::println("Entity {} moved to ({},{})", e.index, pos->x, pos->y);
    }
}
```

`Ref<T>` is read-only. Use `RefMut<T>` when you also need to mutate:

```cpp
void clamp_positions(Query<Item<RefMut<Position>>> query) {
    for (auto&& [pos] : query.iter()) {
        if (pos->x > 100.0f) {
            pos->x = 100.0f;
            pos.set_modified(); // explicit, but auto-set via operator->
        }
    }
}
```

`RefMut<T>` methods:
- `is_added()` / `is_modified()` — change-detection queries
- `set_modified()` — mark as modified in the current tick
- `set_added()` — mark as both added and modified in the current tick

### Change detection on resources — `Res<T>` / `ResMut<T>`

```cpp
void react_to_config(Res<GameConfig> config) {
    if (config.is_modified()) {
        std::println("Config changed: fps={}", config->fps);
    }
}
```

### Change detection in filters — `Added<T>` and `Modified<T>`

Use `Added<T>` or `Modified<T>` as a query filter to skip entities whose component `T` was not added or mutated since the system last ran:

```cpp
void on_health_spawn(
    Query<Item<Entity, const Health&>, Added<Health>> query) {
    for (auto&& [e, hp] : query.iter()) {
        std::println("Entity {} just got a Health component", e.index);
    }
}

void on_health_change(
    Query<Item<Entity, const Health&>, Modified<Health>> query) {
    for (auto&& [e, hp] : query.iter()) {
        std::println("Entity {} health changed to {}", e.index, hp.value);
    }
}
```

Both filters are **non-archetypal** — they filter per-entity at runtime rather than by archetype membership at query-build time. `Modified<T>` reuses the same fetch state as `Added<T>` but compares the `modified` tick instead of `added`.

### `Tick` directly

`Tick` is a 32-bit wrapping counter:

```cpp
Tick a(100), b(200);
bool newer = b.newer_than(a, b);     // true — b is newer than a relative to tick b
Tick diff  = b.relative_to(a);       // Tick(100)
```

Key constants:
- `Tick::max()` — maximum representable change age (`MAX_CHANGE_AGE`)
- `CHECK_TICK_THRESHOLD` — threshold after which ticks are clamped by `check_change_tick()`

## `copy_ref<T>` — opt-in value copy in `Ref<T>`

By default `Ref<T>` holds a pointer to the component. For small, trivially-copyable types a copy may be preferable:

```cpp
// In your component header:
template <>
struct epix::core::copy_ref<glm::vec2> : std::true_type {};

// Now Ref<glm::vec2> stores a value copy instead of a pointer
void check(Query<Item<Ref<glm::vec2>>> q) {
    for (auto&& [v] : q.iter()) {
        glm::vec2 copy = *v; // already a copy
    }
}
```

Required: `copy_ref<T>` must be specialized before any `Ref<T>` is instantiated.

## `Ticks` and `TicksMut` (internal)

These internal structs wrap tick pointers and implement `is_added()` / `is_modified()`. They are exposed in the API but typically used only when implementing custom `SystemParam` or `WorldQuery` types.

## Constraints / Gotchas

- `is_modified()` returns `true` on the frame a component is added (because both `added` and `modified` ticks are set on insertion).
- `is_added()` is only `true` on the **first** frame after insertion. On subsequent frames it is `false`.
- Mutation via `operator->` on `RefMut<T>` sets the modified tick. Direct pointer manipulation via `RefMut::ptr()` does **not** set it — call `set_modified()` explicitly in that case.
- Ticks can saturate if the engine runs for a very long time without calling `check_change_tick()`. `App::update()` calls this automatically.

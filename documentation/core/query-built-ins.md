# Query Built-ins

Special types usable as query data descriptors: `Entity`, `EntityLocation`, `Opt<T&>`, `Ref<T>`, and `RefMut<T>`.

## Overview

These types slot into the `Item<...>` list of a `Query` or `Single` and provide identity, location, optional access, or change-detection wrappers.

## `Entity`

Fetches the id of the matched entity.

```cpp
void log_entities(Query<Item<Entity, const Position&>> query) {
    for (auto&& [entity, pos] : query.iter()) {
        std::println("id={} gen={}", entity.index, entity.generation);
    }
}
```

`Entity` is always `readonly` — it never mutates the world.

## `EntityLocation`

Fetches the archetype/table location of the matched entity.

```cpp
void inspect(Query<Item<Entity, EntityLocation>> query) {
    for (auto&& [e, loc] : query.iter()) {
        std::println("archetype={}", loc.archetype_id.get());
    }
}
```

Useful for debugging or interoperating with lower-level storage APIs.

## `Opt<T&>` — optional component reference

Wrap a component reference inside `Opt<>` to match entities that may or may not have the component.

```cpp
void maybe_health(
    Query<Item<Entity, Opt<const Health&>>> query
) {
    for (auto&& [e, health] : query.iter()) {
        // health is std::optional<std::reference_wrapper<const Health>>
        if (health) {
            std::println("HP: {}", health->get().value);
        }
    }
}
```

`Opt<Health&>` (mutable):
```cpp
void maybe_damage(Query<Item<Opt<Health&>>> query) {
    for (auto&& [health] : query.iter()) {
        if (health) health->get().value -= 10;
    }
}
```

## `Ref<T>` and `RefMut<T>` — change-detection wrappers

Use `Ref<T>` or `RefMut<T>` instead of `const T&` / `T&` to access change-detection metadata alongside the value.

```cpp
void detect_changes(Query<Item<Ref<Position>>> query) {
    for (auto&& [pos_ref] : query.iter()) {
        if (pos_ref.is_added())    std::println("New entity!");
        if (pos_ref.is_modified()) std::println("Position changed!");
        std::println("({}, {})", pos_ref->x, pos_ref->y);
    }
}
```

`RefMut<T>` provides `.set_modified()` and `.set_added()` in addition to the read methods:

```cpp
void mark_modified(Query<Item<RefMut<Position>>> query) {
    for (auto&& [pos_mut] : query.iter()) {
        pos_mut->x += 1.0f;
        pos_mut.set_modified(); // explicit mark (auto-set when you mutate via operator->)
    }
}
```

### `copy_ref<T>` trait

By default `Ref<T>` holds a pointer. For trivially-copyable types where a copy is cheaper than an indirection, specialize `copy_ref<T>`:

```cpp
template <>
struct epix::core::copy_ref<glm::vec2> : std::true_type {};
// Now Ref<glm::vec2> stores a copy instead of a pointer
```

## Built-in Query Filters

### `Added<T>` and `Modified<T>`

Used in the **filter** position of a `Query` to skip entities where component `T` has not been freshly added or mutated:

```cpp
// Only entities that received a Velocity component since last run
void on_velocity_added(Query<Item<Entity>, Added<Velocity>> q) {
    for (auto&& [e] : q.iter())
        std::println("Entity {} got Velocity", e.index);
}

// Only entities whose Health value changed since last run
void on_health_changed(Query<Item<Entity, const Health&>, Modified<Health>> q) {
    for (auto&& [e, hp] : q.iter())
        std::println("Entity {} hp={}", e.index, hp.value);
}
```

Both are **non-archetypal** filters (`archetypal = false`) — they do not narrow the archetype set at query-build time; every matching archetype is still iterated and each entity is checked individually.

| Filter        | Tick checked    | `archetypal` |
| ------------- | --------------- | ------------ |
| `Added<T>`    | `added` tick    | `false`      |
| `Modified<T>` | `modified` tick | `false`      |

## Summary Table

**Query data (go in `Item<...>`):**

| Type             | `readonly` | Change info                                     | Notes                    |
| ---------------- | ---------- | ----------------------------------------------- | ------------------------ |
| `Entity`         | yes        | no                                              | entity id                |
| `EntityLocation` | yes        | no                                              | archetype/table location |
| `Opt<const T&>`  | yes        | no                                              | optional const ref       |
| `Opt<T&>`        | no         | no                                              | optional mutable ref     |
| `Ref<T>`         | yes        | `is_added()`, `is_modified()`                   | immutable + ticks        |
| `RefMut<T>`      | no         | `is_added()`, `is_modified()`, `set_modified()` | mutable + ticks          |

**Query filters (go in the `F` position):**

| Type          | Matches when                           | `archetypal` |
| ------------- | -------------------------------------- | ------------ |
| `Added<T>`    | component T was added since last run   | no           |
| `Modified<T>` | component T was mutated since last run | no           |
| `With<T>`     | entity has component T                 | yes          |
| `Without<T>`  | entity lacks component T               | yes          |
| `Or<F...>`    | any of the inner filters matches       | mixed        |

## Constraints / Gotchas

- `Opt<T&>` does **not** filter archetypes — every entity in the query's archetype set is visited, with `std::nullopt` for missing components.
- Using `Ref<T>` or `RefMut<T>` counts as a read or write access for conflict checking, the same as `const T&` or `T&`.
- `Ref<T>` and `const T&` cannot coexist in the same system for the same component type (access conflict).

# Query

Component iteration over entities: `Query<D, F>`, `Single<D, F>`, and the `WorldQuery`/`QueryData`/`QueryFilter` extension points.

## Overview

A `Query<D, F>` lets a system iterate over all entities that have the component set described by `D` and pass the filter `F`.

- **`D`** — query data: one or more component references or other `world_query` types, typically wrapped in `Item<...>`
- **`F`** — query filter: `With<Ts...>`, `Without<Ts...>`, `Or<Fs...>`, or a tuple of those  (default `Filter<>` matches everything)

## Usage

### Basic iteration

```cpp
// Read-only access to all entities with both Position and Velocity:
void read_system(Query<Item<Entity, const Position&, const Velocity&>> query) {
    for (auto&& [entity, pos, vel] : query.iter()) {
        std::println("Entity {}: ({},{})", entity.index, pos.x, vel.dx);
    }
}

// Mutable access + filter:
void move_system(Query<Item<Position&, const Velocity&>, With<Player>> query) {
    for (auto&& [pos, vel] : query.iter()) {
        pos.x += vel.dx;
        pos.y += vel.dy;
    }
}
```

### Optional components

Wrap the component reference in `Opt<>` to make it optional:

```cpp
void optional_system(
    Query<Item<Entity, Opt<const Health&>, const Position&>> query
) {
    for (auto&& [entity, health_opt, pos] : query.iter()) {
        // health_opt is std::optional<std::reference_wrapper<const Health>>
        if (health_opt) {
            std::println("HP: {}", health_opt->get().value);
        }
    }
}
```

### Single entity

```cpp
// Returns std::nullopt if 0 or more than 1 entity matches
auto maybe = query.single();
if (maybe) { auto&& [pos, vel] = *maybe; }

// Single<D, F> as a system parameter — skips the system if count != 1
void camera_system(Single<Item<Position&>, With<Camera>> cam) {
    cam->x = target_x; // auto-unwrapped
}
```

### Getting a specific entity

```cpp
void lookup_system(Query<Item<const Position&>> query, Res<TargetEntity> target) {
    if (auto opt = query.get(target->entity); opt) {
        auto& [pos] = *opt;
    }
}
```

### Read-only projection

```cpp
auto ro = query.as_readonly(); // Query<ReadOnly<D>, F>
```

### Filters

```cpp
Query<Item<const Position&>, With<Player>>                // has Player
Query<Item<const Position&>, Without<Invisible>>          // lacks Invisible
Query<Item<const Position&>, Or<With<Player>, With<NPC>>> // has Player or NPC
```

Multiple filters in a tuple are ANDed:
```cpp
Query<Item<const Position&>, std::tuple<With<Player>, Without<Dead>>>
```

## Extending — Custom `WorldQuery` / `QueryData` / `QueryFilter`

### `WorldQuery<T>` — customization point for per-entity fetch

```cpp
struct MyData { int value; };

template <>
struct epix::core::WorldQuery<MyData> {
    using Fetch = int*;           // per-archetype fetch state
    using State = TypeId;         // init-time state

    static Fetch init_fetch(World& world, const State& state, Tick lr, Tick tr) { ... }
    static void  set_archetype(Fetch& f, const State& s, const Archetype& a, Table& t) { ... }
    static void  set_access(State& s, const FilteredAccess& access) {}
    static void  update_access(const State& s, FilteredAccess& access) { ... }
    static State init_state(World& world) { return world.type_registry().type_id<MyData>(); }
    static std::optional<State> get_state(const Components& c) { ... }
    static bool  matches_component_set(const State& s, const std::function<bool(TypeId)>& has) { ... }
};
```

### `QueryData<T>` — associate an Item type with a WorldQuery

```cpp
template <>
struct epix::core::QueryData<MyData> {
    using Item    = MyData;       // type returned by the iterator
    using ReadOnly = MyData;      // read-only version
    static constexpr bool readonly = true;
    static Item fetch(WorldQuery<MyData>::Fetch& f, Entity e, TableRow row) { ... }
};
```

### `QueryFilter<T>` — customization point for per-entity filtering

```cpp
template <>
struct epix::core::QueryFilter<MyFilter> {
    static constexpr bool archetypal = true; // if true, checked at archetype level only
    static bool filter_fetch(WorldQuery<MyFilter>::Fetch& f, Entity e, TableRow row) {
        return /* condition */;
    }
};
```

## Concepts

| Concept                  | Description                                            |
| ------------------------ | ------------------------------------------------------ |
| `world_query<T>`         | Types usable as data or filter descriptors             |
| `query_data<T>`          | Types with `QueryData<T>::Item` defined                |
| `query_filter<T>`        | Types with `QueryFilter<T>::filter_fetch` defined      |
| `readonly_query_data<T>` | `query_data<T>` where `QueryData<T>::readonly == true` |

## Constraints / Gotchas

- `const T&` in `Item<...>` gives a read-only reference; `T&` gives a mutable reference. Do not mix `T&` with `const T&` for the same component across multiple params in one system — this is an access conflict detected at startup.
- `single()` returns the first match (not guaranteed unique). Use `Single<D,F>` as a system parameter to assert uniqueness and skip the system when the count is wrong.
- Iterating over an empty query is safe and free.
- `Query::get(entity)` is O(1) — it uses the entity's archetype location to find the component directly.

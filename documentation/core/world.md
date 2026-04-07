# World

The ECS container owning all entities, components, resources, and archetypes.

## Overview

`World` is non-copyable and movable. It is the single source of truth for all game data:
- **Entities** — lightweight ids (`Entity`) managed by the `Entities` allocator
- **Components** — stored in archetype-based tables (dense) or sparse sets (opt-in)
- **Resources** — per-type singleton values with change-detection ticks
- **Archetypes** — groupings of entities that share the same component set

In normal use you do not call `World` directly; systems receive typed params like `Query<...>`, `Res<T>`, and `Commands`. Direct `World` access is appropriate in startup code, tests, or custom runners.

## Usage

### Spawn and insert components

```cpp
World world(WorldId(1));

// Spawn immediately — returns EntityWorldMut for further mutation
auto e = world.spawn(Position{0, 0}, Velocity{1, 2});
Entity id = e.id();

// Insert components on an existing entity
world.get_entity_mut(id).value().get().insert(Health{100});
```

Important: `world.spawn(...)` calls `flush()` internally, applying any pending deferred commands first.

### Resources

```cpp
// Insert
world.insert_resource(GameConfig{fps: 60});

// Init (default or FromWorld-constructed)
world.init_resource<Score>();

// Read
const GameConfig& cfg = world.resource<GameConfig>();

// Mutate
world.resource_mut<Score>().value++;

// Optional access
auto opt = world.get_resource<Score>(); // std::optional<std::reference_wrapper<Score>>

// Resource scope — safe mutation when borrow lifetime is a concern
world.resource_scope([](Score& score, GameConfig& cfg) {
    score.value += cfg.points_per_kill;
});
```

### Querying entities

```cpp
QueryState<Item<Entity, const Position&>, With<Velocity>> state =
    QueryState<Item<Entity, const Position&>, With<Velocity>>::create(world);
auto query = state.query_with_ticks(world, world.last_change_tick(), world.change_tick());
for (auto&& [entity, pos] : query.iter()) { /* ... */ }
```

In a system, prefer using `Query<...>` as a parameter — the state is managed automatically.

### Change ticks

```cpp
Tick current = world.change_tick();
world.check_change_tick([](Tick tick) {
    // Called if ticks were clamped; useful for propagating to other structures
});
```

### Direct entity mutation

```cpp
world.get_entity_mut(entity_id).and_then([](EntityWorldMut&& e) -> std::optional<bool> {
    e.insert(Scale{2.0f});
    e.remove<Velocity>();
    return true;
});
```

### Entity structure

```cpp
export struct Entity {
    uint32_t generation;  // incremented when slot is recycled
    uint32_t index;       // slot index
    uint64_t uid;         // combined id
};
```

Two entities are equal iff both their `generation` and `index` match. Stale entity ids from before recycling will have a mismatched `generation`.

### Clearing the world

```cpp
world.clear_entities();    // remove all entities and components
world.clear_resources();   // remove all resources
```

## Constraints / Gotchas

- `World` is non-copyable. Only one owner exists at a time.
- `spawn()` calls `flush()` which applies deferred command queues first. Entity ids reserved via `Commands` become real only after a flush.
- `change_tick()` can wrap around after ~4 billion increments. The `check_change_tick()` mechanism clamps old ticks automatically within `App::update()`.
- Directly calling `resource_mut<T>()` does **not** mark the resource as modified for change detection. Mutation through `ResMut<T>` in a system does mark it.
- Do not hold references to components/resources across spawns or structural changes (inserting/removing components). The underlying storage may reallocate.

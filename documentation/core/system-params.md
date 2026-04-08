# System Params

How systems receive typed access to the ECS world: the `SystemParam` extension point, all built-in parameters, and how to create custom params using `from_param`.

## Overview

Every argument of a system function is a **system parameter**. The engine fetches all parameters before each system run. If any parameter fails validation (e.g. a required resource is absent), the system is skipped.

The `system_param` concept governs what types are valid:
```cpp
template <typename T>
concept system_param = requires {
    typename SystemParam<T>::State;   // persistent per-system state
    typename SystemParam<T>::Item;    // the value passed to the function
    // ... plus init_state, get_param, validate_param, apply, queue, new_archetype, init_access
};
```

## Built-in Parameters

### `Res<T>` and `ResMut<T>`

Read-only and mutable resource access. Fail validation if the resource is absent.

```cpp
void read_config(Res<GameConfig> cfg) {
    std::println("fps: {}", cfg->fps);
    // cfg.is_added()    — true if resource was added this tick
    // cfg.is_modified() — true if resource was mutated since last run
}

void increment_score(ResMut<Score> score) {
    score->value++;
    // Marks the resource as modified for change detection
}
```

Use `std::optional<Res<T>>` / `std::optional<ResMut<T>>` to make a resource optional (system runs even when the resource is absent):

```cpp
void maybe_update(std::optional<ResMut<Score>> score) {
    if (score) score.value()->value++;
}
```

### `Commands`

Deferred entity and resource operations. Applied after the schedule flush.

```cpp
void spawn_system(Commands cmd) {
    // Spawn an entity with components
    cmd.spawn(Position{0,0}, Velocity{1,1});

    // Spawn and chain further operations
    cmd.spawn_empty()
        .insert(Position{5, 5})
        .insert(Velocity{0, 0});

    // Resource commands
    cmd.insert_resource(Score{0});
    cmd.remove_resource<OldResource>();
}
```

`EntityCommands` is returned by `cmd.spawn(...)`, `cmd.spawn_empty()`, and `cmd.entity(id)`:
```cpp
EntityCommands ec = cmd.entity(existing_id);
ec.insert(Tag{});
ec.remove<OldTag>();
ec.despawn();
ec.spawn(ChildPos{}).insert(ChildTag{}); // spawn a child
```

### `Local<T>`

Per-system persistent state, initialized once via `FromWorld<T>` (default-constructed if no `from_world` method).

```cpp
void counting_system(Local<int> counter) {
    ++(*counter);
    std::println("Run #{}", *counter);
}
```

### `World&`

Exclusive world access. The system becomes an exclusive system — it cannot run in parallel with any other system.

```cpp
void exclusive_system(World& world) {
    world.spawn(Marker{});
}
```

### `Query<D, F>` and `Single<D, F>`

See [query.md](./query.md).

### `EventReader<T>` and `EventWriter<T>`

See [events.md](./events.md).

### `Deferred<T>` and `DeferredWorld`

Low-level deferred buffer access. Prefer `Commands` for most use cases.

```cpp
void low_level(Deferred<CommandQueue> queue) {
    queue->push([](World& w) { /* custom world mutation */ });
}
```

## Extending — Custom System Params via `from_param`

The simplest way to create a custom system param is with a static `from_param` method. The method's arguments must themselves be valid system params.

```cpp
struct PlayerQuery {
    Query<Item<Entity, Position&>, With<Player>> inner;

    static PlayerQuery from_param(
        Query<Item<Entity, Position&>, With<Player>> q
    ) {
        return PlayerQuery{std::move(q)};
    }

    // convenience methods...
    Entity first_player() { return std::get<0>(*inner.single()); }
};

// Use directly in a system:
void move_player(PlayerQuery players, Res<Input> input) {
    // ...
}
```

The `from_param` concept checks that `T::from_param` exists, its arguments are valid system params, and it returns `T`. The engine automatically generates a `SystemParam<PlayerQuery>` specialization.

## Extending — Full `SystemParam` Specialization

For complete control, specialize `SystemParam<T>` directly:

```cpp
namespace epix::core {

template <>
struct SystemParam<MyParam> : ParamBase {
    using State                    = MyState;
    using Item                     = MyParam;
    static constexpr bool readonly = true;  // set false if mutating

    static State init_state(World& world) {
        return MyState{/* ... */};
    }
    static void init_access(const State&, SystemMeta&, FilteredAccessSet&, const World&) {
        // call access.add_unfiltered_resource_read(type_id) etc. to register conflicts
    }
    static std::expected<void, ValidateParamError> validate_param(
        State& state, const SystemMeta&, World& world
    ) {
        // return std::unexpected(ValidateParamError{...}) to skip the system
        return {};
    }
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        return MyParam{/* build from state + world */};
    }
    // apply(), queue(), new_archetype() inherited as no-ops from ParamBase
};

} // namespace epix::core
```

Required members:

| Member                                               | Required | Description                                                        |
| ---------------------------------------------------- | -------- | ------------------------------------------------------------------ |
| `State`                                              | yes      | Persistent per-system state (must be `std::movable`)               |
| `Item`                                               | yes      | Must equal `T`                                                     |
| `readonly`                                           | yes      | `true` if param does not mutate the world                          |
| `init_state(World&)`                                 | yes      | Called once when system is initialized                             |
| `get_param(State&, const SystemMeta&, World&, Tick)` | yes      | Called before each system run                                      |
| `validate_param(...)`                                | yes      | Return `std::unexpected` to skip execution                         |
| `init_access(...)`                                   | yes      | Register component/resource access for conflict checking           |
| `apply(...)`                                         | no       | Apply deferred changes after system run (inherit from ParamBase)   |
| `queue(...)`                                         | no       | Queue deferred changes (inherit from ParamBase)                    |
| `new_archetype(...)`                                 | no       | Called when a new archetype is registered (inherit from ParamBase) |

## Constraints / Gotchas

- `Res<T>` and `ResMut<T>` cannot coexist in the same system for the same `T` — this is a compile-time access conflict.
- `World&` makes the system exclusive. Do not combine it with `Query<>` in the same system.
- `Local<T>` state is stored per-`System` instance, not per-system-type. Two copies of the same function have independent `Local` states.
- `std::optional<Res<T>>` / `std::optional<ResMut<T>>` suppress the validation check for that param. Other params still validate.

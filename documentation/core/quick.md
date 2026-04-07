# EPIX ENGINE CORE MODULE

The ECS backbone of epix_engine: world, entities, components, resources, schedules, systems, events, and the top-level `App` runner.

## Core Parts

- **[`App`](./app.md)**: top-level application — owns a `World`, a schedule order, sub-apps, plugins, and a runner
- **[`World`](./world.md)**: ECS container — entities, components, resources, and archetypes
- **[`Schedule`](./schedule.md)**: named, dependency-ordered system collection with parallel execution
- **[Built-in schedules](./built-in-schedules.md)**: `Startup`, `Update`, `Last`, `OnEnter`, `OnExit`, etc.
- **[System params](./system-params.md)**: `Res<T>`, `ResMut<T>`, `Local<T>`, `Commands`, `World&`, and the `SystemParam` extension point
- **[`Query`](./query.md)**: component iteration — `Query<D,F>`, `Single<D,F>`, `With`, `Without`, `Or`
- **[Query built-ins](./query-built-ins.md)**: `Entity`, `EntityLocation`, `Opt<T&>`, `Ref<T>`, `RefMut<T>` as query data
- **[`Events`](./events.md)**: `Events<T>`, `EventReader<T>`, `EventWriter<T>` — double-buffered event queue
- **[`State`](./state.md)**: `State<T>`, `NextState<T>`, `in_state`, `OnEnter()`/`OnExit()`/`OnChange()` state machines
- **[`Bundle`](./bundle.md)**: struct-of-components customization point — `Bundle<T>`, `is_bundle`
- **[Hierarchy](./hierarchy.md)**: `Parent` and `Children` — propagating entity despawn
- **[Change detection](./change-detection.md)**: `Tick`, `Ref<T>`, `RefMut<T>`, `copy_ref`, `is_added()`/`is_modified()`
- **[Component hooks](./component-hooks.md)**: `ComponentHooks`, `HookContext` — lifecycle callbacks on add/insert/remove/despawn
- **[Labels](./labels.md)**: `Label`, `SystemSetLabel`, `ScheduleLabel`, `AppLabel`

## Quick Guide

```cpp
import epix.core;
using namespace epix::core;

// --- Define components and resources ---
struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct GameConfig { int max_speed = 100; };

// --- Define systems ---
void spawn_entities(Commands commands) {
    commands.spawn(Position{0, 0}, Velocity{1, 2});
    commands.spawn(Position{10, 5}, Velocity{-1, 0});
    commands.insert_resource(GameConfig{});
}

void move_entities(Query<Item<Position&, const Velocity&>> query) {
    for (auto&& [pos, vel] : query.iter()) {
        pos.x += vel.dx;
        pos.y += vel.dy;
    }
}

void print_positions(Query<Item<Entity, const Position&>> query) {
    for (auto&& [entity, pos] : query.iter()) {
        std::println("Entity {}: ({}, {})", entity.index, pos.x, pos.y);
    }
}

// --- Create and run the app ---
int main() {
    App::create()
        .add_systems(Startup, into(spawn_entities))
        .add_systems(Update,  into(move_entities, print_positions))
        .run();
}
```

`App::create()` installs the `LoopPlugin` and `MainSchedulePlugin`.
`into(...)` wraps one or more system functions into a `SetConfig` ready for `add_systems`.
Systems run each frame in the order determined by their dependency graph.

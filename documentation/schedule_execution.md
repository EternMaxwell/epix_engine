# Schedule Execution System - Bevy-like Implementation

This document describes how to use the schedule execution system in epix_engine, which is similar to Rust Bevy's ECS scheduler.

## Overview

The schedule execution system in epix_engine provides a powerful way to organize and execute systems in parallel with automatic dependency tracking. It's conceptually similar to Bevy's schedule system with the following features:

- **Multiple Schedules**: Organize systems into different schedules (like Update, FixedUpdate, etc.)
- **System Ordering**: Use `before()`, `after()`, and `in_set()` to control execution order
- **System Sets**: Group related systems together
- **Conditional Execution**: Use `run_if()` to conditionally execute systems
- **Parallel Execution**: Systems run in parallel when dependencies allow
- **Automatic Dependency Tracking**: Based on system parameters (resources, queries)
- **Hierarchical System Sets**: Create parent-child relationships between sets

## Core Concepts

### Schedules

A `Schedule` is a container for systems that execute together. You typically create multiple schedules for different phases of your application:

```cpp
#include "epix/core/schedule/schedule.hpp"

using namespace epix::core::schedule;

// Define schedule labels
EPIX_MAKE_LABEL(UpdateSchedule);
EPIX_MAKE_LABEL(FixedUpdateSchedule);
EPIX_MAKE_LABEL(RenderSchedule);

// Create schedules
Schedule update_schedule(UpdateSchedule{});
Schedule fixed_update_schedule(FixedUpdateSchedule{});
Schedule render_schedule(RenderSchedule{});
```

### System Sets

System sets are used to group related systems and define execution order at a higher level:

```cpp
// Define system set labels
EPIX_MAKE_LABEL(InputSet);
EPIX_MAKE_LABEL(PhysicsSet);
EPIX_MAKE_LABEL(MovementSet);
EPIX_MAKE_LABEL(RenderSet);

// Configure set ordering
schedule.configure_sets(
    sets(InputSet{}).before(MovementSet{})
);
schedule.configure_sets(
    sets(PhysicsSet{}).before(MovementSet{})
);
schedule.configure_sets(
    sets(MovementSet{}).before(RenderSet{})
);
```

### Adding Systems

Systems are added to schedules using `add_systems()`:

```cpp
// Add a single system
schedule.add_systems(
    into(my_system)
        .set_name("my_system")
        .after(some_other_system)
);

// Add multiple systems
schedule.add_systems(
    into(system1, system2, system3)
        .set_names(std::array{"sys1", "sys2", "sys3"})
        .in_set(MovementSet{})
);

// Chain systems (run in sequence)
schedule.add_systems(
    into(system_a, system_b, system_c)
        .chain()  // system_a -> system_b -> system_c
);
```

## System Ordering

### Using `before()` and `after()`

Control the execution order of individual systems:

```cpp
void input_system() { /* ... */ }
void movement_system() { /* ... */ }
void render_system() { /* ... */ }

schedule.add_systems(into(input_system).set_name("input"));
schedule.add_systems(into(movement_system).set_name("movement").after(input_system));
schedule.add_systems(into(render_system).set_name("render").after(movement_system));
```

### Using System Sets

Organize systems into sets for cleaner dependency management:

```cpp
// Configure set ordering
schedule.configure_sets(sets(InputSet{}).before(GameLogicSet{}));
schedule.configure_sets(sets(GameLogicSet{}).before(RenderSet{}));

// Add systems to sets
schedule.add_systems(into(handle_keyboard).in_set(InputSet{}));
schedule.add_systems(into(handle_mouse).in_set(InputSet{}));
schedule.add_systems(into(update_physics).in_set(GameLogicSet{}));
schedule.add_systems(into(draw_sprites).in_set(RenderSet{}));
```

### Chaining Systems

When you need systems to run sequentially within a larger parallel graph:

```cpp
schedule.add_systems(
    into(
        prepare_render,
        execute_render,
        finalize_render
    )
    .chain()  // These run in order
    .in_set(RenderSet{})
);
```

## Conditional Execution

Use `run_if()` to conditionally execute systems:

```cpp
// Resource for game state
struct GameState {
    bool is_paused;
};

// Condition function
bool is_not_paused(Res<GameState> state) {
    return state && !state->is_paused;
}

// Add system with condition
schedule.add_systems(
    into(update_game_logic)
        .run_if(is_not_paused)
);

// Multiple conditions (all must pass)
bool has_players(/* ... */) { return true; }

schedule.add_systems(
    into(spawn_enemies)
        .run_if(is_not_paused)
        .run_if(has_players)
);
```

## Parallel Execution

Systems run in parallel automatically when their dependencies allow:

```cpp
// These systems can run in parallel (no shared mutable access)
void update_positions(Query<Item<Position&, const Velocity&>> query) { /* ... */ }
void update_animations(Query<Item<Sprite&, const AnimationState&>> query) { /* ... */ }

schedule.add_systems(into(update_positions));
schedule.add_systems(into(update_animations));
// Both can run simultaneously
```

Systems with conflicting access run sequentially:

```cpp
void system_a(Query<Item<Position&>> query) { /* ... */ }
void system_b(Query<Item<Position&>> query) { /* ... */ }

// These will run sequentially due to conflicting mutable access to Position
```

## Complete Example

Here's a complete example showing all features together:

```cpp
#include "epix/core/schedule/schedule.hpp"
#include "epix/core/app/schedules.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::schedule;
using namespace epix::core::app;

// Components
struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct Player {};

// Schedule labels
EPIX_MAKE_LABEL(UpdateSchedule);

// System set labels
EPIX_MAKE_LABEL(InputSet);
EPIX_MAKE_LABEL(MovementSet);
EPIX_MAKE_LABEL(RenderSet);

// Systems
void handle_input(Query<Item<Velocity&, const Player&>> query) {
    // Handle input and update velocity
}

void apply_velocity(Query<Item<Position&, const Velocity&>> query) {
    for (auto&& [pos, vel] : query.iter()) {
        pos.x += vel.dx;
        pos.y += vel.dy;
    }
}

void render_entities(Query<Item<const Position&>> query) {
    // Render entities
}

int main() {
    // Create world and schedules
    World world(WorldId(1));
    Schedules schedules;
    
    // Create and configure update schedule
    Schedule update_schedule(UpdateSchedule{});
    
    // Configure system sets
    update_schedule.configure_sets(sets(InputSet{}).before(MovementSet{}));
    update_schedule.configure_sets(sets(MovementSet{}).before(RenderSet{}));
    
    // Add systems
    update_schedule.add_systems(
        into(handle_input)
            .set_name("handle_input")
            .in_set(InputSet{})
    );
    
    update_schedule.add_systems(
        into(apply_velocity)
            .set_name("apply_velocity")
            .in_set(MovementSet{})
    );
    
    update_schedule.add_systems(
        into(render_entities)
            .set_name("render_entities")
            .in_set(RenderSet{})
    );
    
    // Prepare schedule (validates and builds execution graph)
    auto result = update_schedule.prepare(true);
    if (!result.has_value()) {
        // Handle error
        return 1;
    }
    
    // Add to schedules collection
    schedules.add_schedule(std::move(update_schedule));
    world.insert_resource(std::move(schedules));
    
    // Execute schedule
    auto& world_schedules = world.resource_mut<Schedules>().value();
    auto& update_sched = world_schedules.schedule_mut(UpdateSchedule{});
    
    SystemDispatcher dispatcher(world, 4);
    update_sched.initialize_systems(world);
    
    // Game loop
    while (true) {
        update_sched.execute(dispatcher);
        dispatcher.wait();
        // ... check exit condition
    }
    
    return 0;
}
```

## Advanced Features

### Hierarchical System Sets

Create parent-child relationships between sets:

```cpp
EPIX_MAKE_LABEL(ParentSet);
EPIX_MAKE_LABEL(ChildSetA);
EPIX_MAKE_LABEL(ChildSetB);

// Configure hierarchy
schedule.configure_sets(
    sets(ChildSetA{}, ChildSetB{})
        .in_set(ParentSet{})
);

// Systems in child sets will respect parent set ordering
schedule.configure_sets(sets(ParentSet{}).before(SomeOtherSet{}));
```

### Run-Once Systems

Systems that run only once and then are removed:

```cpp
schedule.set_default_execute_config(ExecuteConfig{
    .run_once = true
});

// Or per-execution
ExecuteConfig config;
config.run_once = true;
schedule.execute(dispatcher, config);
```

### Deferred System Application

Control when system changes are applied:

```cpp
ExecuteConfig config;
config.apply_direct = false;  // Don't apply immediately
config.queue_deferred = false;
config.handle_deferred = true;
config.is_apply_end(); // Apply at end of schedule

schedule.execute(dispatcher, config);
```

## Comparison with Bevy

| Feature | Bevy | epix_engine |
|---------|------|-------------|
| Multiple Schedules | ✅ `Schedules` resource | ✅ `Schedules` resource |
| System Sets | ✅ `SystemSet` trait | ✅ `SystemSetLabel` |
| System Ordering | ✅ `before()`, `after()`, `in_set()` | ✅ `before()`, `after()`, `in_set()` |
| Conditional Systems | ✅ `run_if()` | ✅ `run_if()` |
| System Chaining | ✅ `chain()` | ✅ `chain()` |
| Parallel Execution | ✅ Automatic | ✅ Automatic via `SystemDispatcher` |
| Schedule Labels | ✅ `ScheduleLabel` | ✅ `ScheduleLabel` |
| Commands | ✅ `Commands` | ✅ `Commands` |
| Queries | ✅ `Query<T>` | ✅ `Query<Item<T>>` |
| Resources | ✅ `Res<T>`, `ResMut<T>` | ✅ `Res<T>` |

## Best Practices

1. **Use System Sets**: Group related systems into sets for better organization
2. **Name Your Systems**: Always use `set_name()` for better debugging
3. **Prepare Once**: Call `prepare()` once after adding all systems
4. **Initialize Systems**: Call `initialize_systems()` before first execution
5. **Use Conditions Wisely**: Conditions are checked every frame, keep them lightweight
6. **Leverage Parallelism**: Let the scheduler handle parallelism, don't force sequential execution unless necessary
7. **Handle Errors**: Check the result of `prepare()` for cycle detection
8. **Thread Pool Size**: Choose an appropriate thread count for your `SystemDispatcher`

## Common Patterns

### Startup Systems

```cpp
schedule.add_systems(
    into(setup_world)
        .set_name("setup")
);

// Later, use run_once to remove after first execution
ExecuteConfig config;
config.run_once = true;
schedule.execute(dispatcher, config);
```

### State-Based Execution

```cpp
struct GameState { enum class State { Menu, Playing, Paused } state; };

bool in_menu(Res<GameState> state) { return state && state->state == GameState::State::Menu; }
bool in_game(Res<GameState> state) { return state && state->state == GameState::State::Playing; }

schedule.add_systems(into(show_menu).run_if(in_menu));
schedule.add_systems(into(update_game).run_if(in_game));
```

### Fixed Timestep

```cpp
Schedule fixed_update(FixedUpdateSchedule{});
// Add physics systems
fixed_update.add_systems(into(physics_step));

// In game loop
while (accumulator >= FIXED_TIMESTEP) {
    fixed_update.execute(dispatcher);
    dispatcher.wait();
    accumulator -= FIXED_TIMESTEP;
}
```

## See Also

- [System Parameter Documentation](systems.md)
- [Query Documentation](queries.md)
- [Commands Documentation](commands.md)
- [App Documentation](app/app.md)

# Bevy-like Schedule Execution System

This directory contains documentation for the schedule execution system in epix_engine, which provides Bevy-like ECS scheduling capabilities.

## What is it?

The schedule execution system allows you to:
- Organize systems into schedules (like Update, FixedUpdate, Render)
- Define execution order using `before()`, `after()`, and `in_set()`
- Run systems conditionally with `run_if()`
- Execute systems in parallel automatically
- Group related systems into sets
- Handle deferred operations

## Key Features

✅ **Multiple Schedules** - Create different schedules for different purposes  
✅ **System Ordering** - Control when systems run relative to each other  
✅ **System Sets** - Group and order related systems together  
✅ **Conditional Execution** - Run systems only when conditions are met  
✅ **Parallel Execution** - Automatic parallelization based on data access  
✅ **Hierarchical Sets** - Create parent-child set relationships  
✅ **Change Detection** - Track when data changes  
✅ **Deferred Operations** - Queue operations to apply later  

## Quick Start

```cpp
#include "epix/core/schedule/schedule.hpp"
#include "epix/core/app/schedules.hpp"

using namespace epix::core;
using namespace epix::core::schedule;

// 1. Define schedule label
EPIX_MAKE_LABEL(UpdateSchedule);

// 2. Create schedule
Schedule update_schedule(UpdateSchedule{});

// 3. Add systems
update_schedule.add_systems(
    into(my_system)
        .set_name("my_system")
);

// 4. Prepare and execute
update_schedule.prepare(true);
update_schedule.initialize_systems(world);

SystemDispatcher dispatcher(world);
update_schedule.execute(dispatcher);
dispatcher.wait();
```

## Documentation

- **[Schedule Execution Guide](../schedule_execution.md)** - Complete user guide with examples
  - How to create and use schedules
  - System ordering and sets
  - Conditional execution
  - Best practices and common patterns

- **[Technical Details](../schedule_execution_technical.md)** - Implementation details
  - How the executor works internally
  - Comparison with Bevy's implementation
  - Performance characteristics
  - Debugging tips

- **[Example Test](../../epix_engine/core/tests/schedule_bevy_like.cpp)** - Working example
  - Demonstrates all major features
  - Shows Bevy-like usage patterns
  - Ready to compile and run

## Comparison with Bevy

The epix_engine schedule system is very similar to Bevy's:

| Feature | Bevy | epix_engine | Notes |
|---------|------|-------------|-------|
| Multiple Schedules | ✅ | ✅ | Use `Schedules` resource |
| System Sets | ✅ | ✅ | `SystemSetLabel` |
| System Ordering | ✅ | ✅ | `before()`, `after()`, `in_set()` |
| Conditional Systems | ✅ | ✅ | `run_if()` |
| System Chaining | ✅ | ✅ | `chain()` |
| Parallel Execution | ✅ | ✅ | Automatic |
| Schedule Labels | ✅ | ✅ | `EPIX_MAKE_LABEL` |
| Commands | ✅ | ✅ | Deferred operations |
| Queries | ✅ | ✅ | Component access |
| Resources | ✅ | ✅ | Singleton data |
| Change Detection | ✅ | ✅ | Tick-based |

## Example Usage

### Basic Schedule

```cpp
// Define labels
EPIX_MAKE_LABEL(UpdateSchedule);
EPIX_MAKE_LABEL(MovementSet);

// Create schedule
Schedule schedule(UpdateSchedule{});

// Add systems to a set
schedule.add_systems(
    into(apply_velocity)
        .set_name("apply_velocity")
        .in_set(MovementSet{})
);

// Execute
schedule.prepare(true);
schedule.initialize_systems(world);

SystemDispatcher dispatcher(world);
schedule.execute(dispatcher);
```

### System Ordering

```cpp
// System B runs after System A
schedule.add_systems(into(system_a).set_name("system_a"));
schedule.add_systems(
    into(system_b)
        .set_name("system_b")
        .after(system_a)
);

// Or use sets
schedule.configure_sets(sets(SetA{}).before(SetB{}));
schedule.add_systems(into(system_a).in_set(SetA{}));
schedule.add_systems(into(system_b).in_set(SetB{}));
```

### Conditional Execution

```cpp
// Define condition
bool is_running(Res<GameState> state) {
    return state && !state->paused;
}

// Add system with condition
schedule.add_systems(
    into(update_game)
        .run_if(is_running)
);
```

### System Chaining

```cpp
// These run in sequence
schedule.add_systems(
    into(prepare, execute, finalize)
        .chain()
);
```

### Multiple Schedules

```cpp
Schedules schedules;

// Create multiple schedules
Schedule update(UpdateSchedule{});
Schedule fixed_update(FixedUpdateSchedule{});
Schedule render(RenderSchedule{});

// Add systems to each
update.add_systems(into(game_logic));
fixed_update.add_systems(into(physics));
render.add_systems(into(draw));

// Store in world
schedules.add_schedule(std::move(update));
schedules.add_schedule(std::move(fixed_update));
schedules.add_schedule(std::move(render));

world.insert_resource(std::move(schedules));

// Execute different schedules at different times
auto& scheds = world.resource_mut<Schedules>().value();
scheds.schedule_mut(UpdateSchedule{}).execute(dispatcher);
scheds.schedule_mut(FixedUpdateSchedule{}).execute(dispatcher);
scheds.schedule_mut(RenderSchedule{}).execute(dispatcher);
```

## Common Patterns

### Startup Systems

```cpp
ExecuteConfig config;
config.run_once = true;  // Remove after first execution

schedule.add_systems(into(setup));
schedule.execute(dispatcher, config);
```

### State-Based Systems

```cpp
enum class GameState { Menu, Playing, Paused };

bool in_menu(Res<GameState> state) {
    return state && *state == GameState::Menu;
}

bool playing(Res<GameState> state) {
    return state && *state == GameState::Playing;
}

schedule.add_systems(into(show_menu).run_if(in_menu));
schedule.add_systems(into(update_game).run_if(playing));
```

### Fixed Timestep

```cpp
const float FIXED_DT = 1.0f / 60.0f;
float accumulator = 0.0f;

while (running) {
    float dt = get_delta_time();
    accumulator += dt;
    
    while (accumulator >= FIXED_DT) {
        fixed_update.execute(dispatcher);
        dispatcher.wait();
        accumulator -= FIXED_DT;
    }
    
    update.execute(dispatcher);
    dispatcher.wait();
}
```

## Getting Started

1. Read the [Schedule Execution Guide](../schedule_execution.md)
2. Look at the [example test](../../epix_engine/core/tests/schedule_bevy_like.cpp)
3. Try creating your own schedule
4. Check the [technical details](../schedule_execution_technical.md) if you need more information

## See Also

- [App Documentation](../app/app.md) - Application structure
- [System Documentation](systems.md) - How to write systems
- [Query Documentation](queries.md) - Querying entities
- [Commands Documentation](commands.md) - Entity manipulation
- [Bevy Documentation](https://bevyengine.org/learn/book/getting-started/ecs/) - Original inspiration

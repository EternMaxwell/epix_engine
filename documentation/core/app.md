# App

The top-level application object that owns the `World`, schedules, sub-apps, plugins, and the main-loop runner.

## Overview

`App` is the entry point for every epix program. It wires together:
- A `World` (entities + components + resources)
- A `Schedules` map and `ScheduleOrder` list controlling per-frame execution
- A `Plugins` registry
- An optional set of named sub-apps (for render/extract patterns)
- An `AppRunner` that drives the main loop

`App` is non-copyable and movable.

## Usage

### Creating and running an app

```cpp
import epix.core;
using namespace epix::core;

int main() {
    App::create()
        .add_systems(Startup, into([] { std::println("Hello!"); }))
        .add_systems(Update,  into([] { /* per-frame work */ }))
        .run();
}
```

`App::create()` installs `LoopPlugin` and `MainSchedulePlugin`. Call `run()` to hand control to the configured runner.

### Adding systems

```cpp
app.add_systems(Update, into(my_system));
app.add_systems(Update, into(sys_a, sys_b).chain()); // sys_a before sys_b
app.add_systems(Update, into(sys_a).after(sys_b));   // explicit ordering
```

### Adding resources

```cpp
// Directly, before running:
app.world_mut().insert_resource(MyResource{42});

// Via Commands in a system:
void startup(Commands cmd) {
    cmd.insert_resource(MyResource{42});
}
```

### Plugins

A plugin is any type that exposes `void build(App&)` and/or `void finish(App&)`, or is directly callable as `void(App&)`.

```cpp
struct MyPlugin {
    void build(App& app) {
        app.add_systems(Update, into(my_system));
    }
};

app.add_plugin<MyPlugin>();
// or, passing a constructed value:
app.add_plugins(MyPlugin{});
```

`build()` runs immediately when `add_plugin` is called.
`finish()` runs just before `run()`.
`finalize()` (optional) runs after the main loop exits.

Plugin access after construction:
```cpp
app.plugin<MyPlugin>();          // const ref, throws if absent
app.get_plugin<MyPlugin>();      // std::optional<ref>
app.plugin_scope([](MyPlugin& p) { /* exclusive access */ });
```

### Events

```cpp
app.add_event<MyEvent>();
// Then in systems:
void producer(EventWriter<MyEvent> writer) { writer.write(MyEvent{42}); }
void consumer(EventReader<MyEvent> reader) {
    for (auto evt : reader.read()) { /* ... */ }
}
```

### State machines

```cpp
enum class AppState { Menu, Playing, Paused };

app.insert_state(AppState::Menu);
app.add_systems(OnEnter(AppState::Playing), into(start_game));
app.add_systems(Update,
    into(game_update).run_if(in_state(AppState::Playing)));
```

### Sub-apps

```cpp
app.add_sub_app(RenderAppLabel{});
App& render = app.sub_app_mut(RenderAppLabel{});
```

### Custom runner

```cpp
struct MyRunner : AppRunner {
    bool step(App& app) override {
        app.update();
        return !should_quit;
    }
    void exit(App& app) override {
        app.run_schedules(PreExit, Exit, PostExit);
    }
};
app.set_runner(std::make_unique<MyRunner>());
app.run();
```

## Extending / Custom Plugins

```cpp
struct PhysicsPlugin {
    float gravity = 9.81f;

    void build(App& app) {
        app.world_mut().insert_resource(Gravity{gravity});
        app.add_systems(Update, into(physics_step));
    }
    // void finish(App& app) — optional, called just before run()
    // void finalize(App& app) — optional, called after loop exits
};
```

The `is_plugin` concept accepts:
- Struct with `void build(App&)`
- Struct with `void finish(App&)` (no `build` required)
- Callable `void(App&)` (e.g. a lambda)

## Constraints / Gotchas

- `App` is non-copyable. Pass by reference or move.
- `add_plugin<T>` is a no-op if `T` was already added (deduplication by type). The plugin check happens inside `Plugins`, so duplicate `add_plugin` calls are silently ignored.
- `run()` throws if no runner has been set.
- `world_mut()` / `world()` give direct access — useful in setup; avoid using them inside running systems (use system params instead).
- Schedule order is respected only for schedules inside `ScheduleOrder`; schedules created outside it run only when explicitly requested.
- `add_systems(Update, ...)` creates the `Update` schedule if it doesn't exist, but the schedule will not run unless it is in the `ScheduleOrder` (which `MainSchedulePlugin` sets up for all built-ins).

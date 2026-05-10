# InputPlugin

Registers all input infrastructure — events, resources, and frame-update systems — into the `App`.

## Overview

`InputPlugin::build()` does three things:

1. Calls `app.add_events<T>()` for `KeyInput`, `MouseButtonInput`, `MouseMove`, and `MouseScroll`.
2. Calls `app.world_mut().init_resource<ButtonInput<KeyCode>>()` and `init_resource<ButtonInput<MouseButton>>()`.
3. Adds `ButtonInput<KeyCode>::collect_events` and `ButtonInput<MouseButton>::collect_events` to the `First` schedule so button state is ready before `Update` systems run.

The plugin does **not** produce the raw events itself — a window backend (e.g. `glfw::GLFWPlugin`) must be added alongside it to push events into the queues.

## Usage

```cpp
import epix.core;
import epix.input;

using namespace epix;

int main() {
    core::App app = core::App::create();
    app.add_plugins(core::TaskPoolPlugin{})
       .add_plugins(window::WindowPlugin{ /* ... */ })
       .add_plugins(input::InputPlugin{})   // registers events + ButtonInput resources
       .add_plugins(glfw::GLFWPlugin{})     // produces the raw events
       // ...
       .run();
}
```

## `log_inputs` — Debug System

`log_inputs` is an exported free function with the signature of a system. When added to a schedule it logs every key press/release, mouse button press/release, mouse move delta, and scroll offset to `spdlog` at `info` level.

```cpp
// Add for a single run to verify events are flowing:
app.add_systems(core::Update, core::into(input::log_inputs));
```

Remove it from the schedule in production — it logs every event every frame at `info` level.

## Constraints

- `InputPlugin` must be added before calling `app.run()`.
- The plugin is idempotent in the sense that `init_resource` is a no-op if the resource already exists, but `add_events` called twice would register duplicates. Add the plugin exactly once.

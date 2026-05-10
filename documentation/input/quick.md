# EPIX ENGINE INPUT MODULE

Provides keyboard and mouse input as ECS events and per-frame button state resources, wired into the app via a single plugin.

## Core Parts

- **[`InputPlugin`](./input-plugin.md)**: registers input event types, initializes `ButtonInput` resources, and adds the `collect_events` systems in the `First` schedule.
- **[`KeyInput`](./events.md)**: event fired each frame for every key press or release (includes repeat).
- **[`MouseButtonInput`](./events.md)**: event fired on mouse button press or release.
- **[`MouseMove`](./events.md)**: event fired with the (dx, dy) cursor delta whenever the mouse moves.
- **[`MouseScroll`](./events.md)**: event fired with horizontal/vertical scroll offsets.
- **[`ButtonInput<KeyCode>`](./button-input.md)**: resource that tracks held/just-pressed/just-released keyboard key state, updated each `First` schedule.
- **[`ButtonInput<MouseButton>`](./button-input.md)**: resource that tracks held/just-pressed/just-released mouse button state, updated each `First` schedule.
- **[`KeyCode`](./enums.md)**: enum of physical keyboard key identifiers.
- **[`MouseButton`](./enums.md)**: enum of mouse button identifiers with `Left`/`Right`/`Middle` aliases.
- **[`log_inputs`](./input-plugin.md)**: optional debug system that logs all received input events.

## Quick Guide

```cpp
import epix.core;
import epix.input;

using namespace epix;

// 1. Add the plugin during app setup (before run).
//    This registers all four event types and the ButtonInput resources.
app.add_plugins(input::InputPlugin{});

// 2. Query button state in a system using Res<ButtonInput<T>>.
void move_player(
    core::Res<input::ButtonInput<input::KeyCode>> keys,
    core::Res<input::ButtonInput<input::MouseButton>> mouse_btns)
{
    if (keys->pressed(input::KeyCode::KeyW)) { /* move forward */ }
    if (keys->just_pressed(input::KeyCode::KeySpace)) { /* jump */ }
    if (mouse_btns->pressed(input::MouseButton::MouseButtonRight)) { /* aim */ }
}

// 3. Read raw events in a system using EventReader<T>.
void on_scroll(core::EventReader<input::MouseScroll> scroll) {
    for (const auto& e : scroll.read()) {
        float zoom_delta = static_cast<float>(e.yoffset);
        // apply zoom ...
    }
}

// 4. Register the system.
app.add_systems(core::Update, core::into(move_player, on_scroll));
```

> The window/GLFW plugin is responsible for producing the raw events consumed by `InputPlugin`.
> Add `glfw::GLFWPlugin` (or equivalent) before or alongside `InputPlugin`.

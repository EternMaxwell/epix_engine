# EPIX ENGINE WINDOW MODULE

Manages OS windows as ECS entities — create, configure, and react to window events through a backend-agnostic interface backed by either GLFW or SFML.

## Core Parts

- **[`Window`](./window.md)**: Component attached to an entity to create and control a native OS window. Owns all display properties (size, position, title, cursor, mode, etc.).
- **[`PrimaryWindow`](./window.md#primarywindow)**: Marker component designating the main window; used by exit-condition logic.
- **[`CachedWindow`](./window.md#cachedwindow)**: Read-only snapshot of a `Window` from the previous frame; used to detect changes without mutation.
- **[`WindowPlugin`](./window.md#windowplugin)**: Plugin that registers all window events and creates the primary window entity.
- **[`ExitCondition`](./window.md#windowplugin)**: Enum controlling when the app auto-exits (all closed / primary closed / never).
- **[Window Events](./events.md)**: Structs broadcast when windows are created, resized, moved, focused, closed, or destroyed; also covers cursor and file-drop events.
- **[`GLFWPlugin` / `GLFWRunner`](./glfw-backend.md)**: GLFW windowing backend. Drives the event loop and native window lifecycle.
- **[`SFMLPlugin` / `SFMLRunner`](./sfml-backend.md)**: SFML windowing backend. Equivalent to GLFWPlugin for SFML-based projects.
- **[`Clipboard`](./glfw-backend.md#clipboard)**: Resource exposing the system clipboard for read and write (available in both backends).
- **[`log_events`](./events.md#log_events)**: Debug system that logs all window events to the logger.

## Quick Guide

```cpp
import epix.core;
import epix.window;
import epix.glfw.core; // or epix.sfml.core

int main() {
    using namespace epix::core;
    using namespace epix::window;
    using namespace epix::glfw;

    App app = App::create();

    app.add_plugins(TaskPoolPlugin{})
        // WindowPlugin registers events and spawns the primary window.
        .add_plugins(WindowPlugin{})
        .plugin_scope([](WindowPlugin& p) {
            p.primary_window = Window{
                .title = "My App",
                .size  = {1280, 720},
            };
            p.exit_condition = ExitCondition::OnPrimaryClosed;
        })
        // Add the windowing backend (GLFW or SFML).
        .add_plugins(GLFWPlugin{})
        // React to window events in a system.
        .add_systems(Update, into([](EventReader<WindowResized> ev) {
            for (auto&& [window, w, h] : ev.read()) {
                // handle resize
            }
        }));

    app.run();
}
```

### Spawning additional windows at runtime

```cpp
// In a Startup system or directly on the world:
app.world_mut().spawn(Window{
    .title = "Second Window",
    .size  = {800, 600},
});
```

### Modifying a window at runtime

Mutate the `Window` component; the backend syncs changes on the next frame.

```cpp
// In an Update system:
void toggle_fullscreen(EventReader<epix::input::KeyInput> keys,
                       Query<Item<Mut<Window>>> windows) {
    for (auto&& [key, scancode, pressed, repeat, win_entity] : keys.read()) {
        if (key == epix::input::KeyCode::KeyF11 && pressed) {
            if (auto opt = windows.get(win_entity)) {
                auto&& [win] = *opt;
                win->window_mode = (win->window_mode == WindowMode::Windowed)
                    ? WindowMode::Fullscreen : WindowMode::Windowed;
            }
        }
    }
}
```

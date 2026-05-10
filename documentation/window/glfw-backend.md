# GLFW Backend

GLFW-based windowing backend (`epix.glfw.core`) — implements the native window lifecycle, event polling, and input dispatch for projects that use GLFW.

## Overview

`GLFWPlugin` wires all backend systems into the app. `GLFWRunner` replaces the default app runner and drives the GLFW event loop each frame. The two can be used independently when a custom runner is needed.

---

## `GLFWPlugin`

Plugin that registers all GLFW backend systems. Add it after `WindowPlugin`.

```cpp
import epix.glfw.core;

app.add_plugins(WindowPlugin{})
   .add_plugins(epix::glfw::GLFWPlugin{});
```

**What it registers:**
- Window creation, state sync, mode toggle, and destruction systems.
- `poll_events` + `send_cached_events` for dispatching GLFW callbacks to ECS events.
- Clipboard read/write systems.
- Sets `GLFWRunner` as the app runner.

### Static Systems

These are not normally called directly but can be referenced for ordering:

| System | When it runs |
|---|---|
| `GLFWPlugin::create_windows` | Creates native `GLFWwindow*` for new `Window` entities |
| `GLFWPlugin::update_size` | Syncs native window size → `Window::size` |
| `GLFWPlugin::update_pos` | Syncs native window position → `Window::final_pos` |
| `GLFWPlugin::update_window_states` | Applies title, cursor, icon, opacity changes |
| `GLFWPlugin::toggle_window_mode` | Applies fullscreen / windowed mode transitions |
| `GLFWPlugin::send_cached_events` | Flushes GLFW callbacks into ECS event queues |
| `GLFWPlugin::destroy_windows` | Destroys native window when entity is despawned |

---

## `GLFWRunner`

Application runner that owns the GLFW per-frame loop. Installed automatically by `GLFWPlugin`.

```cpp
struct GLFWRunner : public AppRunner {
    GLFWRunner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;

    void set_render_app(const core::AppLabel& label) noexcept;
    void reset_render_app() noexcept;
    void append_system(std::unique_ptr<core::System<std::tuple<>, void>> system);
};
```

### Render Sub-App

`GLFWRunner` supports running a render sub-app on a separate thread concurrently with the main world:

```cpp
// After adding GLFWPlugin, configure the runner:
app.runner_mut<GLFWRunner>().set_render_app(RenderAppLabel{});
```

Call `reset_render_app()` to revert to single-threaded mode.

---

## `GLFWwindows`

Resource mapping entity IDs to native `GLFWwindow*` pointers. Useful for direct GLFW API calls.

```cpp
import <GLFW/glfw3.h>;

void my_system(Res<epix::glfw::GLFWwindows> glfw_windows, Query<Item<Entity>> entities) {
    for (auto&& [entity] : entities) {
        auto it = glfw_windows->find(entity);
        if (it != glfw_windows->end()) {
            GLFWwindow* native = it->second;
            // use GLFW API directly
        }
    }
}
```

---

## `Clipboard`

Resource providing read access to the system clipboard. Updated each frame by `Clipboard::update`.

```cpp
// Read clipboard text:
void read_clip(Res<epix::glfw::Clipboard> clipboard) {
    const std::string& text = clipboard->get_text();
}
```

### `SetClipboardString`

Event that writes text to the clipboard. Send it from any system:

```cpp
void write_clip(EventWriter<epix::glfw::SetClipboardString> writer) {
    writer.write(epix::glfw::SetClipboardString{.text = "hello"});
}
```

---

## `PathDrop`

Backend-internal event carrying raw file paths from GLFW drop callbacks, before they are forwarded to the engine's `FileDrop` event. In normal usage, read [`FileDrop`](./events.md#filedrop) instead.

---

## Constraints / Gotchas

- GLFW must be called from the main OS thread. Do not call `GLFWwindows` systems from a worker thread.
- The render sub-app thread shares no ECS data directly; use the `Extract<T>` mechanism.
- `update_pos` uses a local pending-positions map to handle async window-manager position settling.
- `Window::cursor_pos` and read-only state fields are filled by GLFW callbacks; do not set them manually.

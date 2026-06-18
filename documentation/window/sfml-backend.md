# SFML Backend

SFML-based windowing backend (`epix.sfml.core`) — implements the native window lifecycle, event polling, and input dispatch for projects that use SFML.

## Overview

`SFMLPlugin` is the SFML counterpart to [`GLFWPlugin`](./glfw-backend.md). The public interface is identical: add the plugin, optionally configure the render sub-app, and use the same `Window` component and events. Only the underlying native handles differ.

---

## `SFMLPlugin`

Plugin that registers all SFML backend systems. Add it after `WindowPlugin`.

```cpp
import epix.sfml.core;

app.add_plugins(WindowPlugin{})
   .add_plugins(epix::sfml::SFMLPlugin{});
```

**What it registers:**
- Window creation, state sync, mode toggle, and destruction systems.
- `poll_and_send_events` for dispatching SFML events to ECS events (including input).
- Clipboard read/write systems.
- Sets `SFMLRunner` as the app runner.

### Static Systems

| System | When it runs |
|---|---|
| `SFMLPlugin::create_windows` | Creates native `sf::WindowBase` for new `Window` entities |
| `SFMLPlugin::update_size` | Syncs native window size → `Window::size` |
| `SFMLPlugin::update_pos` | Syncs native window position → `Window::final_pos`; handles settle retries via `PendingWindowPositions` |
| `SFMLPlugin::update_window_states` | Applies title, cursor, icon, opacity changes |
| `SFMLPlugin::toggle_window_mode` | Applies fullscreen / windowed mode transitions |
| `SFMLPlugin::poll_and_send_events` | Polls SFML events and writes to ECS event queues |
| `SFMLPlugin::destroy_windows` | Destroys native window when entity is despawned |

---

## `SFMLRunner`

Application runner that owns the SFML per-frame loop. Installed automatically by `SFMLPlugin`.

```cpp
struct SFMLRunner : public AppRunner {
    SFMLRunner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;

    void set_render_app(const core::AppLabel& label) noexcept;
    void reset_render_app() noexcept;
    void append_system(std::unique_ptr<core::System<std::tuple<>, void>> system);
};
```

### Render Sub-App

Identical to the GLFW runner — configure a render sub-app for concurrent rendering:

```cpp
app.runner_mut<SFMLRunner>().set_render_app(RenderAppLabel{});
```

---

## `SFMLwindows`

Resource mapping entity IDs to `shared_ptr<sf::WindowBase>` pointers. Useful for direct SFML API calls.

```cpp
void my_system(Res<epix::sfml::SFMLwindows> sfml_windows, Query<Item<Entity>> entities) {
    for (auto&& [entity] : entities) {
        auto it = sfml_windows->find(entity);
        if (it != sfml_windows->end()) {
            auto& native = it->second; // shared_ptr<sf::WindowBase>
            // use SFML API directly
        }
    }
}
```

---

## `Clipboard`

Same interface as the GLFW `Clipboard`. Updated each frame; send `SetClipboardString` to write.

```cpp
// Read:
void read_clip(Res<epix::sfml::Clipboard> clipboard) {
    const std::string& text = clipboard->get_text();
}

// Write:
void write_clip(EventWriter<epix::sfml::SetClipboardString> writer) {
    writer.write(epix::sfml::SetClipboardString{.text = "hello"});
}
```

---

## `PendingWindowPositions` / `PendingWindowPosition`

Internal resource used by `update_pos` to retry position enforcement while the window manager is settling after a move request. Not normally accessed directly.

```cpp
struct PendingWindowPosition {
    std::pair<int, int> target = {0, 0};
    int retries_remaining      = 0;
};
struct PendingWindowPositions : std::unordered_map<Entity, PendingWindowPosition> {};
```

---

## Constraints / Gotchas

- SFML must be driven from the main OS thread; do not call `SFMLwindows` from worker threads.
- The render sub-app thread shares no ECS data directly; use the `Extract<T>` mechanism.
- `PendingWindowPositions` may hold a target for several frames after a position change while the WM settles.
- `Window::cursor_pos` and other read-only fields are filled by the SFML event loop; do not set them manually.
- Unlike GLFW, SFML uses `sf::WindowBase` (no OpenGL context) — rendering is expected to go through WebGPU or a separate surface.

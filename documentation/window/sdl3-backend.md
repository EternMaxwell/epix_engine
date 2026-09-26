# SDL3 Backend

SDL3-based windowing backend (`epix.sdl3.core`) — implements the native window lifecycle and event dispatch for every Epix project. It is the only window backend.

## Overview

`SDL3Plugin` wires the backend systems into the app and installs `SDL3Runner`, which hands the main loop to SDL's application callbacks. The two can be used independently when a custom runner is needed.

SDL is a **private** dependency of the engine: no public header includes an SDL header, and `SDL3` is linked into the engine rather than into consumers. SDL keeps process-wide state (its window registry, its property sets), so a second copy inside an executable would not see the windows the engine created.

```cpp
import epix.sdl3.core;

app.add_plugins(WindowPlugin{})
   .add_plugins(epix::sdl3::SDL3Plugin{});
```

---

## `SDL3Plugin`

Plugin that registers the SDL3 backend systems. Add it after `WindowPlugin`.

**What it registers:**
- Window creation, state sync, mode toggle, and destruction systems.
- `send_cached_events` for translating SDL's events into ECS events.
- Clipboard read/write systems.
- Sets `SDL3Runner` as the app runner.

### Static Systems

These are not normally called directly but can be referenced for ordering:

| System | When it runs |
|---|---|
| `SDL3Plugin::create_windows` | Creates a native `SDL_Window*` for new `Window` entities |
| `SDL3Plugin::update_size` | Syncs native window size, pixel size and scale → `Window` |
| `SDL3Plugin::update_pos` | Syncs native window position → `Window::final_pos` |
| `SDL3Plugin::update_window_states` | Applies title, cursor, icon, opacity changes |
| `SDL3Plugin::toggle_window_mode` | Applies fullscreen / borderless / windowed transitions |
| `SDL3Plugin::send_cached_events` | Translates the frame's staged SDL events into ECS events |
| `SDL3Plugin::update_clipboard` | Mirrors the `Clipboard` resource and the OS clipboard |
| `SDL3Plugin::destroy_windows` | Destroys the native window when the entity is despawned |
| `SDL3Plugin::destroy_all_windows` | Destroys every remaining native window at shutdown |

---

## `SDL3Runner`

Application runner that owns the main loop. Installed automatically by `SDL3Plugin`.

```cpp
struct SDL3Runner : public AppRunner {
    SDL3Runner(App& app);
    bool step(App& app) override;
    void exit(App& app) override;

    bool iterate();                       // one engine frame
    void stage_event(const void* event);  // one SDL_Event, opaque
    void on_sdl_quit();

    void set_render_app(const app::AppLabel& label) noexcept;
    void reset_render_app() noexcept;
    void append_system(std::unique_ptr<ecs::System<std::tuple<>, void>> system);
};
```

### The main loop belongs to SDL

`step()` calls `SDL_EnterAppMainCallbacks` once and blocks there; SDL then drives
the engine through `iterate()` until the engine reports that it has exited.

That inversion is deliberate. When a Windows window is dragged or resized, the
window manager runs a **modal move/resize loop** inside the application's own
message dispatch: the app's message pump stops returning, so a main loop of its
own stops producing frames and the window freezes until the drag ends. SDL
answers this by installing a timer for the duration of that modal loop and
re-entering the application callbacks from it, so frames keep being produced.
The engine can only benefit from that if it runs *inside* SDL's callbacks,
which is why the runner gives up its own loop.

Measured on this backend on Windows: a 19-second interactive resize produced
824 consecutive frames with a maximum inter-frame gap of 24 ms, and the window
was reported at 25 distinct sizes during the drag.

### Render Sub-App

`SDL3Runner` supports running a render sub-app on a separate thread concurrently with the main world:

```cpp
app.runner_mut<SDL3Runner>().set_render_app(RenderAppLabel{});
```

The render sub-app is stopped *before* the frame that reports termination, because SDL tears its video subsystem down (and destroys every window) as soon as the callbacks report termination.

---

## `SDL3windows`

Resource mapping entity IDs to native `SDL_Window*` pointers.

```cpp
void my_system(Res<epix::sdl3::SDL3windows> sdl_windows, Query<Item<Entity>> entities) {
    for (auto&& [entity] : entities.iter()) {
        auto it = sdl_windows->find(entity);
        if (it != sdl_windows->end()) {
            SDL_Window* native = it->second;
            // hand it to an SDL call
        }
    }
}
```

Reaching the native handle from a consumer that has its own SDL headers is
possible but not encouraged: the engine's SDL and a consumer's SDL would be
different copies.

---

## `SDL3Events`

The raw `SDL_Event` records SDL delivered for the frame that is running, stored
byte-for-byte and opaque so no public header needs SDL. `SDL3Plugin::send_cached_events`
translates them; the runner clears them once the frame's systems have run.

---

## Clipboard

`Clipboard` lives in the common window module, not in a backend; this backend
keeps it in sync with the OS. There is no event for writing it — **writing the
resource is the request**:

```cpp
void write_clip(ResMut<epix::window::Clipboard> clipboard) {
    clipboard->text = "hello";   // the backend pushes this to the OS next frame
}

void read_clip(Res<epix::window::Clipboard> clipboard) {
    const std::string& text = clipboard->text;   // refreshed from the OS each frame
}
```

`SDL3Plugin::update_clipboard` runs once per frame. It keeps a copy of the last
value it pushed, and:

- if the resource was modified (`ResMut::is_modified()`) and its `text` differs
  from that copy, it writes `text` to the OS clipboard;
- otherwise it reads the OS clipboard and publishes it into the resource, so a
  change made by another application shows up.

The read-back goes through `Mut::bypass_change_detection()`, so the backend's
own mirroring is not reported as a change; only other systems' writes are.

---

## Constraints / Gotchas

- SDL is not thread-safe, and a `SendMessage` from a worker thread to a window
  owned by the main thread deadlocks while the main thread is blocked in the
  schedule that is running the worker. Systems that touch the native window must
  therefore run on the thread that created it; the backend's own systems do,
  because the runner executes them from SDL's iteration callback.
- The render sub-app thread shares no ECS data directly; use the `Extract<T>` mechanism.
- `update_pos` uses a local pending-positions map to handle async window-manager position settling.
- `Window::cursor_pos` is read back from SDL as-is and is not clamped to the window, so it may lie outside the client area once the cursor leaves; do not set it manually unless you mean to warp the pointer.
- `CursorMode::Captured` and `CursorMode::Disabled` are both implemented with a pointer clip rectangle (the client area, and a centre lock respectively), which is what SDL offers on Windows.

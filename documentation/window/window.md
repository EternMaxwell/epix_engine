# Window

Component-based OS window — attach a `Window` to an entity to create and control a native window through the active windowing backend.

## Overview

`Window` is the central configuration component. Spawning an entity with a `Window` causes the active backend (`GLFWPlugin` or `SFMLPlugin`) to create a native OS window. Mutating the component's fields on a subsequent frame causes the backend to apply the change (title, cursor, size limits, fullscreen mode, etc.). Read-only fields are written back by the backend each frame (focused, iconified, cursor position, etc.).

The ECS entity representing the window is the authoritative identity. Events carry the entity ID so systems can route event handling per-window.

---

## `Window`

Main configuration component. All fields have reasonable defaults; the minimal usage is to set `title` and `size`.

### Fields

| Field | Type | Default | Notes |
|---|---|---|---|
| `pos_type` | `PosType` | `Centered` | How `pos` is interpreted at creation |
| `pos` | `pair<int,int>` | `{0,0}` | Requested position (meaning depends on `pos_type`) |
| `final_pos` | `pair<int,int>` | `{0,0}` | Actual screen position; takes priority over `pos` when written |
| `size` | `pair<int,int>` | `{1280,720}` | Width × height in pixels |
| `cursor_pos` | `pair<double,double>` | `{0,0}` | Current cursor position in client-area coords (read-only at runtime) |
| `cursor_in_window` | `bool` | `false` | True when cursor is inside the client area (read-only) |
| `frame_size` | `FrameSize` | `{}` | Border/title-bar pixel sizes (read-only) |
| `size_limits` | `SizeLimits` | `{min 160×120, no max}` | Minimum and maximum window size |
| `resizable` | `bool` | `true` | Whether the user can resize the window |
| `decorations` | `bool` | `true` | Whether title bar and borders are shown |
| `visible` | `bool` | `true` | Whether the window is visible |
| `opacity` | `float` | `1.0` | Window opacity (0 = transparent, 1 = opaque) |
| `focused` | `bool` | `false` | Input focus state (read-only) |
| `iconified` | `bool` | `false` | Minimized state (read-only) |
| `maximized` | `bool` | `false` | Maximized state (read-only) |
| `icon` | `optional<Handle<Image>>` | `nullopt` | Optional window icon |
| `cursor_icon` | `CursorIcon` | `StandardCursor::Arrow` | Standard or custom cursor shape |
| `cursor_mode` | `CursorMode` | `Normal` | Cursor visibility/confinement |
| `composite_alpha_mode` | `CompositeAlphaMode` | `Auto` | Surface compositing hint for the render system |
| `present_mode` | `PresentMode` | `AutoNoVsync` | Vsync/presentation hint for the render system |
| `window_level` | `WindowLevel` | `Normal` | Z-order relative to other windows |
| `window_mode` | `WindowMode` | `Windowed` | Windowed / fullscreen / borderless |
| `monitor` | `int` | `0` | Monitor index for fullscreen |
| `title` | `string` | `"untitled"` | Title bar text |
| `attention_request` | `bool` | `false` | Taskbar flash pending |

### Methods

```cpp
// Request user attention (taskbar flash). Clears automatically once handled by the backend.
window.request_attention();

// Cursor position normalised to [-0.5, 0.5] with origin at window centre, +y up.
auto [rx, ry] = window.relative_cursor_pos();
```

### Usage

```cpp
// Spawning a window directly on the world:
app.world_mut().spawn(epix::window::Window{
    .title        = "My Window",
    .size         = {1280, 720},
    .opacity      = 0.9f,
    .resizable    = true,
    .present_mode = epix::window::PresentMode::AutoNoVsync,
});

// Mutating window state at runtime (backend syncs next frame):
void toggle_fullscreen(Query<Item<Mut<Window>>> windows, EventReader<KeyInput> keys) {
    for (auto&& [key, sc, pressed, repeat, win] : keys.read()) {
        if (key == KeyCode::KeyF11 && pressed) {
            if (auto opt = windows.get(win)) {
                auto&& [w] = *opt;
                w->window_mode = (w->window_mode == WindowMode::Windowed)
                    ? WindowMode::Fullscreen : WindowMode::Windowed;
            }
        }
    }
}
```

---

## `PrimaryWindow`

Marker component that designates the main application window. `WindowPlugin` spawns the primary window entity with both `Window` and `PrimaryWindow`. When `exit_condition` is `OnPrimaryClosed`, the app exits when this entity is destroyed.

```cpp
// Mark an existing window entity as primary:
commands.entity(entity).insert(PrimaryWindow{});
```

---

## `CachedWindow`

Read-only alias (`const CachedWindowMut`) that holds a snapshot of a `Window` component from the end of the previous frame. Systems in the backend use this to detect which fields changed and apply only the necessary OS calls.

User systems can query `CachedWindow` to detect frame-to-frame changes:

```cpp
void detect_resize(Query<Item<const Window&, const CachedWindow&>> windows) {
    for (auto&& [win, cached] : windows) {
        if (win.size != cached.size) {
            // window was resized this frame
        }
    }
}
```

---

## `WindowPlugin`

Plugin that registers all window events and optionally spawns the primary window entity.

```cpp
struct WindowPlugin {
    std::optional<Window> primary_window = Window{};   // nullopt = no auto-spawn
    ExitCondition exit_condition = ExitCondition::OnPrimaryClosed;
    bool close_when_requested   = true;                // despawn entity on close request
    void build(App& app);
    void finish(App& app);
};
```

**What it does:**
- Registers all window events (`WindowResized`, `WindowMoved`, `WindowCreated`, etc.).
- In `finish()`: spawns the primary window entity if `primary_window` has a value.
- Adds exit systems (`exit_on_all_closed` or `exit_on_primary_closed`) to `PostUpdate`.
- Adds `close_requested` system to `Update` that despawns an entity when `WindowCloseRequested` is received (if `close_when_requested = true`).

### Usage

```cpp
app.add_plugins(WindowPlugin{})
   .plugin_scope([](WindowPlugin& p) {
       p.primary_window = Window{.title = "My App", .size = {1280, 720}};
       p.exit_condition = ExitCondition::OnAllClosed;
       p.close_when_requested = true;
   });
```

---

## `ExitCondition`

Controls when `WindowPlugin` sends `AppExit`:

| Value | Behaviour |
|---|---|
| `OnPrimaryClosed` | Exit when the entity with `PrimaryWindow` is destroyed (default) |
| `OnAllClosed` | Exit when every tracked window entity is destroyed |
| `None` | Never auto-exit due to window closure |

---

## Supporting Types

### `PosType`

```cpp
enum PosType { TopLeft, Centered, Relative };
```

- `TopLeft`: `pos` is absolute screen coordinates from the top-left of the target monitor.
- `Centered`: window is centred on the target monitor; `pos` is ignored.
- `Relative`: `pos` is offset relative to the parent window's top-left (used with parent–child entity hierarchy).

### `SizeLimits`

```cpp
struct SizeLimits {
    int min_width  = 160;
    int min_height = 120;
    int max_width  = -1;   // -1 = no limit
    int max_height = -1;
};
```

### `FrameSize`

Pixel widths of the window decorations (read-only, written by the backend):

```cpp
struct FrameSize { int left, right, top, bottom; };
```

### `CursorMode`

| Value | Behaviour |
|---|---|
| `Normal` | Cursor visible, free movement |
| `Hidden` | Cursor hidden, position still tracked |
| `Captured` | Cursor hidden, confined to window bounds |
| `Disabled` | Cursor hidden, virtual/unbounded (FPS camera mode) |

### `CursorIcon`

Variant of `StandardCursor` or `CustomCursor`:

```cpp
// Standard shape:
window.cursor_icon = StandardCursor::Hand;

// Custom image:
window.cursor_icon = CustomCursor{
    .image = asset_server.load<image::Image>("cursor.png"),
    .hot_x = 0,
    .hot_y = 0,
};
```

### `WindowMode`

| Value | Behaviour |
|---|---|
| `Windowed` | Normal decorated window |
| `Fullscreen` | Exclusive fullscreen on `monitor` |
| `BorderlessFullscreen` | Borderless window covering the monitor |

### `PresentMode`

Hint read by the **rendering system** (e.g. WebGPU swapchain) — not applied by the windowing backend itself.

| Value | Behaviour |
|---|---|
| `AutoNoVsync` | Prefer `Immediate → Mailbox → Fifo` (lowest latency; default) |
| `AutoVsync` | Prefer `FifoRelaxed → Fifo` |
| `Fifo` | VSync; frame rate capped to monitor refresh |
| `FifoRelaxed` | VSync but tears if frame is late |
| `Immediate` | No VSync, may tear |
| `Mailbox` | Triple-buffer; no tear, low latency |

### `WindowLevel`

```cpp
enum class WindowLevel { AlwaysOnBottom, Normal, AlwaysOnTop };
```

### `CompositeAlphaMode`

Hint read by the **rendering system** for surface compositing — not applied by the windowing backend itself. `Auto` selects the best available mode.

# Window Events

ECS events broadcast by the windowing backend each frame when OS-level window events occur.

## Overview

`WindowPlugin` registers all event types. The active backend (`GLFWPlugin` / `SFMLPlugin`) polls OS events and writes to these queues during the `PreUpdate` phase. Systems in `Update` or later can read them.

All events carry a `core::Entity window` field identifying which window entity produced the event.

---

## Lifecycle Events

### `WindowCreated`

Sent once after the native OS window is created and added to the backend's window map.

```cpp
struct WindowCreated { core::Entity window; };
```

```cpp
void on_created(EventReader<WindowCreated> ev) {
    for (auto&& [win] : ev.read()) {
        // native window for 'win' entity is now live
    }
}
```

### `WindowCloseRequested`

Sent when the OS (or user) requests the window to close (e.g. clicking the X button). If `WindowPlugin::close_when_requested = true`, the entity is automatically despawned; otherwise the game must handle it manually.

```cpp
struct WindowCloseRequested { core::Entity window; };
```

```cpp
void handle_close(EventReader<WindowCloseRequested> ev, Commands cmd) {
    for (auto&& [win] : ev.read()) {
        // optionally show a "save?" dialog before despawning
        cmd.entity(win).despawn();
    }
}
```

### `WindowClosed`

Sent when the `Window` component is removed or the entity is despawned. The native window is about to be destroyed.

```cpp
struct WindowClosed { core::Entity window; };
```

### `WindowDestroyed`

Sent after the native OS window handle has been released. By this point the entity no longer has a live native window.

```cpp
struct WindowDestroyed { core::Entity window; };
```

---

## Geometry Events

### `WindowResized`

Sent when the native window's client area changes size.

```cpp
struct WindowResized {
    core::Entity window;
    int width;
    int height;
};
```

```cpp
void on_resize(EventReader<WindowResized> ev) {
    for (auto&& [win, w, h] : ev.read()) {
        // update viewport, projection matrix, etc.
    }
}
```

### `WindowMoved`

Sent when the window's screen position changes.

```cpp
struct WindowMoved {
    core::Entity window;
    std::pair<int, int> position; // (x, y) top-left in screen coordinates
};
```

---

## Focus Events

### `WindowFocused`

Sent when a window gains or loses keyboard focus.

```cpp
struct WindowFocused {
    core::Entity window;
    bool focused; // true = gained focus, false = lost focus
};
```

```cpp
void on_focus(EventReader<WindowFocused> ev) {
    for (auto&& [win, focused] : ev.read()) {
        if (!focused) { /* pause simulation */ }
    }
}
```

---

## Cursor Events

### `CursorMoved`

Sent every frame that the cursor moves within a window.

```cpp
struct CursorMoved {
    core::Entity window;
    std::pair<double, double> position; // (x, y) in client-area pixels, top-left origin, +y down
    std::pair<double, double> delta;    // (dx, dy) since previous event
};
```

In `CursorMode::Disabled`, `position` is a virtual unbounded value and `delta` is the raw motion.

```cpp
void camera_look(EventReader<CursorMoved> ev) {
    for (auto&& [win, pos, delta] : ev.read()) {
        auto [dx, dy] = delta;
        // apply yaw/pitch
    }
}
```

### `CursorEntered`

Sent when the cursor enters or leaves a window's client area.

```cpp
struct CursorEntered {
    core::Entity window;
    bool entered; // true = entered, false = left
};
```

---

## Input Events

### `ReceivedCharacter`

Sent when the OS delivers a Unicode character input (after IME processing). Useful for text fields.

```cpp
struct ReceivedCharacter {
    core::Entity window;
    char32_t character;
};
```

### `FileDrop`

Sent when the user drops one or more files onto a window.

```cpp
struct FileDrop {
    core::Entity window;
    std::vector<std::string> paths;
};
```

```cpp
void on_drop(EventReader<FileDrop> ev) {
    for (auto&& [win, paths] : ev.read()) {
        for (auto& p : paths) {
            // load file at path p
        }
    }
}
```

---

## `log_events`

Debug system that logs all window events to spdlog. Add to any schedule during development:

```cpp
app.add_systems(Update, into(epix::window::log_events).set_name("log window events"));
```

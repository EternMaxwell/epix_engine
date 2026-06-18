# Input Events

Raw ECS events emitted each frame by the window backend for keyboard, mouse button, mouse movement, and scroll wheel input.

## Overview

Four event types are registered by [`InputPlugin`](./input-plugin.md). Systems read them via `EventReader<T>`. The events are consumed each frame; unconsumed events from the previous frame are not carried forward.

---

## `KeyInput`

Fired once for every key state change (press or release) and once per repeat tick while a key is held.

```cpp
struct KeyInput {
    KeyCode key;       // which key
    int     scancode;  // platform-specific scancode
    bool    pressed;   // true = pressed, false = released
    bool    repeat;    // true = OS-generated key repeat
    Entity  window;    // entity of the window that received the event
};
```

**Usage:**

```cpp
void on_key(core::EventReader<input::KeyInput> reader) {
    for (auto&& [key, scancode, pressed, repeat, window] : reader.read()) {
        if (pressed && !repeat && key == input::KeyCode::KeyF) {
            // F key pressed (not a repeat)
        }
    }
}
```

> Prefer [`ButtonInput<KeyCode>`](./button-input.md) for held-key polling; use `KeyInput` when you need per-event granularity or repeat information.

---

## `MouseButtonInput`

Fired once per mouse button press or release.

```cpp
struct MouseButtonInput {
    MouseButton button;  // which button
    bool        pressed; // true = pressed, false = released
    Entity      window;  // entity of the window that received the event
};
```

**Usage:**

```cpp
void on_click(core::EventReader<input::MouseButtonInput> reader) {
    for (auto&& [button, pressed, window] : reader.read()) {
        if (pressed && button == input::MouseButton::MouseButtonLeft) {
            // left click
        }
    }
}
```

---

## `MouseMove`

Fired whenever the cursor moves. Contains the delta (dx, dy) relative to the previous position, not the absolute cursor position.

```cpp
struct MouseMove {
    std::pair<double, double> delta;  // (dx, dy) movement since last event
};
```

**Usage:**

```cpp
void on_mouse_move(core::EventReader<input::MouseMove> reader) {
    for (auto&& [delta] : reader.read()) {
        auto [dx, dy] = delta;
        // rotate camera by (dx, dy) * sensitivity
    }
}
```

> For absolute cursor position, query the `Window` component's `cursor_pos` field directly.

---

## `MouseScroll`

Fired on scroll wheel or touchpad scroll gestures.

```cpp
struct MouseScroll {
    double xoffset;  // horizontal scroll
    double yoffset;  // vertical scroll (positive = scroll up)
    Entity window;   // entity of the window
};
```

**Usage** (from `cam_controll.hpp`):

```cpp
void camera_zoom(
    core::Query<core::Item<render::camera::Projection&>> camera,
    core::EventReader<input::MouseScroll> scroll_input)
{
    for (const auto& e : scroll_input.read()) {
        float scale = std::exp(-static_cast<float>(e.yoffset) * 0.1f);
        // apply scale to projection ...
    }
}
```

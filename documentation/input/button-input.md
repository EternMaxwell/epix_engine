# ButtonInput

Per-frame button state resource that tracks which keys or mouse buttons are held, just pressed, or just released.

## Overview

`ButtonInput<T>` is a resource updated every `First` schedule by `collect_events`. It consumes the raw `KeyInput` / `MouseButtonInput` events and classifies each button into three disjoint sets:

| Set | Meaning |
|-----|---------|
| `pressed` | Button is currently held down |
| `just_pressed` | Button transitioned from up → down this frame |
| `just_released` | Button transitioned from down → up this frame |

`just_pressed` and `just_released` are cleared at the start of each `collect_events` call, so they are only `true` for the single frame the event occurred.

Two specializations exist: `ButtonInput<KeyCode>` for the keyboard and `ButtonInput<MouseButton>` for the mouse. The primary template `ButtonInput<T>` is empty — only these two specializations are usable.

---

## `ButtonInput<KeyCode>`

### Usage

Inject as `core::Res<input::ButtonInput<input::KeyCode>>` (read-only) in any system.

```cpp
// From epix_engine/experimental/examples/voxel_path_tracer.cpp
void camera_control(
    core::Res<input::ButtonInput<input::KeyCode>> keys,
    // ...
    Query<Item<Mut<transform::Transform>>, With<Camera>> cameras)
{
    glm::vec3 move{0.0f};
    if (keys->pressed(input::KeyCode::KeyW)) move += local_z;
    if (keys->pressed(input::KeyCode::KeyS)) move -= local_z;
    if (keys->pressed(input::KeyCode::KeyA)) move -= local_x;
    if (keys->pressed(input::KeyCode::KeyD)) move += local_x;
    if (keys->just_pressed(input::KeyCode::KeyEscape)) { /* open menu */ }
}
```

### Single-key queries

```cpp
bool just_pressed(KeyCode key)  const noexcept;  // true only on the frame the key went down
bool just_released(KeyCode key) const noexcept;  // true only on the frame the key went up
bool pressed(KeyCode key)       const noexcept;  // true while key is held
```

### Multi-key queries

```cpp
// Range views over the current sets:
auto just_pressed_keys()  const;  // view of all keys just pressed this frame
auto just_released_keys() const;  // view of all keys just released this frame
auto pressed_keys()       const;  // view of all currently held keys

// Shorthand checks over a vector of keys:
bool any_just_pressed(const std::vector<KeyCode>& keys)  const noexcept;
bool any_just_released(const std::vector<KeyCode>& keys) const noexcept;
bool any_pressed(const std::vector<KeyCode>& keys)       const noexcept;
bool all_pressed(const std::vector<KeyCode>& keys)       const noexcept;
```

Example — detect a chord:

```cpp
if (keys->all_pressed({input::KeyCode::KeyLeftControl, input::KeyCode::KeyS})) {
    // Ctrl+S held
}
```

---

## `ButtonInput<MouseButton>`

Identical interface to `ButtonInput<KeyCode>`, but for mouse buttons. Range-view methods are named `just_pressed_buttons()`, `just_released_buttons()`, and `pressed_buttons()`.

### Usage

```cpp
// From epix_engine/experimental/examples/voxel_path_tracer.cpp
void camera_control(
    core::Res<input::ButtonInput<input::MouseButton>> mouse_btns,
    // ...
)
{
    const bool looking = mouse_btns->pressed(input::MouseButton::MouseButtonRight);
    // when looking: read window cursor_pos and compute delta manually
}
```

See [enums.md](./enums.md) for the full list of `MouseButton` values and their `Left`/`Right`/`Middle` aliases.

---

## Bypass Flags

Each specialization exposes three bypass flags that force all queries of the corresponding set to return `false`:

```cpp
void bypass_pressed()       noexcept;  // pressed() → always false
void bypass_just_pressed()  noexcept;  // just_pressed() → always false
void bypass_just_released() noexcept;  // just_released() → always false
void clear_bypass()         noexcept;  // restore normal behavior
```

`clear_bypass()` is called automatically at the start of each `collect_events` tick, so bypass flags set in one frame do not persist into the next unless set again. The intended use is to suppress input in a given frame (e.g. when a UI overlay has focus).

---

## Constraints

- `ButtonInput` resources are initialized by `InputPlugin`. Do not use them without first adding the plugin.
- `just_pressed` and `just_released` are only `true` for a single frame. Check them in `Update` (runs after `First`); they will be cleared before the next `Update`.
- Hold detection deduplicates: if the key is already in `pressed`, a second press event is silently ignored (no duplicate `just_pressed` entry).

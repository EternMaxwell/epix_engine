# KeyCode and MouseButton

Strongly-typed enums for identifying keyboard keys and mouse buttons, with utility functions for human-readable names.

## `KeyCode`

`enum class KeyCode : int` — physical keyboard key identifiers, independent of keyboard layout.

### Key groups

| Group | Members |
|-------|---------|
| Letters | `KeyA` – `KeyZ` |
| Digits | `Key0` – `Key9` |
| Symbols | `KeySpace`, `KeyApostrophe`, `KeyComma`, `KeyMinus`, `KeyPeriod`, `KeySlash`, `KeySemicolon`, `KeyEqual`, `KeyLeftBracket`, `KeyBackslash`, `KeyRightBracket`, `KeyGraveAccent` |
| Navigation | `KeyRight`, `KeyLeft`, `KeyDown`, `KeyUp`, `KeyPageUp`, `KeyPageDown`, `KeyHome`, `KeyEnd`, `KeyInsert`, `KeyDelete` |
| Control | `KeyEscape`, `KeyEnter`, `KeyTab`, `KeyBackspace`, `KeyCapsLock`, `KeyScrollLock`, `KeyNumLock`, `KeyPrintScreen`, `KeyPause` |
| Function | `KeyF1` – `KeyF25` |
| Keypad | `KeyKp0` – `KeyKp9`, `KeyKpDecimal`, `KeyKpDivide`, `KeyKpMultiply`, `KeyKpSubtract`, `KeyKpAdd`, `KeyKpEnter`, `KeyKpEqual` |
| Modifiers | `KeyLeftShift`, `KeyLeftControl`, `KeyLeftAlt`, `KeyLeftSuper`, `KeyRightShift`, `KeyRightControl`, `KeyRightAlt`, `KeyRightSuper`, `KeyMenu` |
| Sentinel | `KeyLast = KeyMenu`, `KeyUnknown = -1` |
| International | `KeyWorld1`, `KeyWorld2` |

### `key_name()`

Returns a human-readable `std::string_view` for a `KeyCode`. The returned view points to a static string and remains valid for the lifetime of the process.

```cpp
std::string_view name = input::key_name(input::KeyCode::KeyLeftShift);
// name == "LeftShift"
```

### Hash support

`std::hash<epix::input::KeyCode>` is specialized, so `KeyCode` values can be used as keys in `std::unordered_map` / `std::unordered_set` directly.

---

## `MouseButton`

`enum class MouseButton : int` — mouse button identifiers.

| Enumerator | Alias | Description |
|------------|-------|-------------|
| `MouseButton1` | `MouseButtonLeft` | Primary button |
| `MouseButton2` | `MouseButtonRight` | Secondary button |
| `MouseButton3` | `MouseButtonMiddle` | Middle / scroll-wheel click |
| `MouseButton4` – `MouseButton8` | — | Extra buttons |
| `MouseButtonLast` | — | Sentinel (equals `MouseButton8`) |
| `MouseButtonUnknown` | — | Value `-1` for unrecognized buttons |

### `mouse_button_name()`

Returns a human-readable `std::string_view` for a `MouseButton`.

```cpp
std::string_view name = input::mouse_button_name(input::MouseButton::MouseButtonLeft);
// name == "Left"
```

### Hash support

`std::hash<epix::input::MouseButton>` is specialized for use in unordered containers.

---

## Usage in practice

`KeyCode` and `MouseButton` are consumed primarily via [`ButtonInput<T>`](./button-input.md) queries and [`KeyInput`/`MouseButtonInput`](./events.md) event structs. They are not constructed directly — the enumerators are used as values.

```cpp
// Checking a specific key:
if (keys->pressed(input::KeyCode::KeyLeftControl) &&
    keys->just_pressed(input::KeyCode::KeyZ)) {
    // Ctrl+Z just pressed
}

// Checking a mouse button:
if (mouse->pressed(input::MouseButton::MouseButtonRight)) {
    // right button held
}
```

# TODO — input

Features that are planned, partially implemented, or have an API stub only.
See also: [project-wide todo](../todo.md)

The `epix.input` module is fully implemented with no known stubs or commented-out features.
All exported functions and specializations have real bodies in `src/button.cpp`, `src/input.cpp`,
and `src/enums.cpp`.

Potential future work (no interface stubs exist yet):

- [ ] **Gamepad / joystick support** — no `GamepadInput` event or `ButtonInput<GamepadButton>` exists.
- [ ] **Text input events** — no character/code-point event (useful for text fields).
- [ ] **Cursor position resource** — absolute cursor position is currently only available via
  the `Window` component's `cursor_pos` field; a dedicated `CursorPos` resource would be more ergonomic.
- [ ] **Input suppression integration** — `bypass_*` flags exist on `ButtonInput` but there is no
  automatic integration with a UI-focus system to toggle them.

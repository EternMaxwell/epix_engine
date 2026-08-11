# TODO — window

Features that are planned, partially implemented, or have an API stub only.  
See also: [project-wide todo](../todo.md)

- [~] **`WindowLevel::AlwaysOnBottom`** — `AlwaysOnBottom` is declared in the
  `WindowLevel` enum, but the GLFW backend only has native handling for the
  always-on-top state and the SFML backend only caches the requested level.
  `AlwaysOnBottom` therefore has no native effect on either backend.

- [~] **Attention request differs between backends** — In GLFW,
  `Window::attention_request` triggers `glfwRequestWindowAttention` (a taskbar
  notification). In SFML the same flag calls `requestFocus()`, which requests
  focus instead. The two backends therefore expose different semantics.

- [ ] **Custom cursor in SFML** — `CursorIcon` accepting a `CustomCursor` (image asset + hotspot) is part of the API but the SFML `update_window_states` system does not yet load the image and create an `sf::Cursor` from it. The standard cursor shapes are applied correctly; custom cursors are silently ignored by the SFML backend.

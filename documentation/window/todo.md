# TODO — window

Features that are planned, partially implemented, or have an API stub only.  
See also: [project-wide todo](../todo.md)

- [~] **`WindowLevel::AlwaysOnBottom`** — `AlwaysOnBottom` is declared in the `WindowLevel` enum but GLFW only supports `GLFW_FLOATING` (always-on-top) via `glfwSetWindowAttrib` (`glfw.cpp:534`). The SFML backend also only caches this field (`sfml.cpp:664`). `AlwaysOnBottom` has no native effect on either backend.

- [~] **Attention request differs between backends** — In GLFW, `Window::attention_request` triggers `glfwRequestWindowAttention` (taskbar flash, `glfw.cpp:394`). In SFML the same flag calls `window->requestFocus()` instead (`sfml.cpp:545`), which requests focus rather than flashing the taskbar — the semantics differ from the documented intent.

- [ ] **Custom cursor in SFML** — `CursorIcon` accepting a `CustomCursor` (image asset + hotspot) is part of the API but the SFML `update_window_states` system does not yet load the image and create an `sf::Cursor` from it. The standard cursor shapes are applied correctly; custom cursors are silently ignored by the SFML backend.

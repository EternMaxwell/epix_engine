# TODO — window

Features that are planned, partially implemented, or have an API stub only.  
See also: [project-wide todo](../todo.md)

- [~] **`WindowLevel::AlwaysOnBottom`** — `AlwaysOnBottom` is declared in the
  `WindowLevel` enum, but SDL3 only models the always-on-top state
  (`SDL_SetWindowAlwaysOnTop`). `AlwaysOnBottom` therefore has no native effect
  and a window asking for it reads back as `Normal`.

- [~] **Window icon cannot be cleared** — `Window::icon` accepts an
  `optional<Handle<Image>>`, and assigning an image installs it through
  `SDL_SetWindowIcon`. SDL has no "restore the default icon" call, so assigning
  `std::nullopt` clears the component field without changing the native icon.

- [~] **`CursorMode::Disabled` needs keyboard focus** — SDL applies relative
  mouse mode to the keyboard-focused window only, so the mode engages when the
  window has focus. The backend re-asserts it whenever the window gains focus,
  which covers the normal case, but a request made while another window holds
  focus does not take effect until focus arrives.

- [ ] **`KeyWorld1` / `KeyWorld2` and `KeyF25` have no SDL key code** — SDL3
  stops at `SDLK_F24` and has no key code for the two "world" keys, so those
  three engine key codes cannot be produced or consumed by this backend.
  `map_key_to_sdl3` maps them to `SDLK_UNKNOWN`.

- [ ] **`MouseButton6`–`MouseButton8` have no SDL counterpart** — the engine
  follows GLFW's eight-button numbering, while SDL reports at most five physical
  buttons (`SDL_BUTTON_LEFT`, `MIDDLE`, `RIGHT`, `X1`, `X2`).
  `map_mouse_button_to_sdl3` returns 0 for the three buttons SDL cannot express.

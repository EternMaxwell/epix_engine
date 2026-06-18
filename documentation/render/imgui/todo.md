# render/imgui — Outstanding Items

No stubs, empty implementations, or `TODO` comments were found in any exported
API of `epix.render.imgui`.

The module is considered feature-complete for out-of-box ImGui usage.

---

## Potential Future Work

| Item | Severity |
|------|----------|
| No opt-in mechanism to disable `imgui_consume_input` — users cannot retain keyboard/mouse events in the engine while ImGui has focus | ergonomic |
| GLFW backend is lazy-initialized on the first frame; there is a one-frame gap before input handling starts | minor |
| `DrawDataSnapshot` clones draw lists by copying raw byte buffers; a buffer-pool could reduce allocations on high-widget-count frames | perf |
| Multi-viewport surfaces are stored in a file-local `g_viewport_surfaces` map; no API to enumerate or manage them from outside the plugin | internal |

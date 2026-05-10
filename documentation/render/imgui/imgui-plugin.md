# ImGuiPlugin, Ctx & ImGuiState

## ImGuiPlugin

```cpp
namespace epix::imgui {
struct ImGuiPlugin {
    bool enable_docking   = false;
    bool enable_viewports = false;

    ImGuiPlugin& set_docking(bool enabled = true) noexcept;
    ImGuiPlugin& set_viewports(bool enabled = true) noexcept;

    void build(App& app);
    void finalize(App& app);
};
}
```

`ImGuiPlugin` is the single entry point for the entire ImGui integration.
Adding it to the app handles every aspect of the ImGui lifecycle — there is
nothing else to configure.

### What `build()` does

1. Creates an `ImGuiContext` and configures:
   - `ImGuiConfigFlags_NavEnableKeyboard` — always on
   - `ImGuiConfigFlags_DockingEnable` — when `enable_docking = true`
   - `ImGuiConfigFlags_ViewportsEnable` — when `enable_viewports = true`
   - `ImGuiBackendFlags_RendererHasTextures` — required for the WebGPU backend
2. Inserts `ImGuiState` into the main world as a resource.
3. Registers `BeginFrameSet` as a system set inside `PreUpdate`.
4. Adds frame lifecycle systems:
   - `imgui_begin_frame` in `PreUpdate` (inside `BeginFrameSet`)
   - `imgui_end_frame` in `Last`
   - `imgui_consume_input` as a post-system that runs after every schedule
5. Adds `ExtractResourcePlugin<ImGuiState>` to copy state into the render world.
6. Adds `imgui_render` to the render sub-app, scheduled after `RenderSet::Render`
   and before `RenderSet::Cleanup`.

### What `finalize()` does

Called during `App::run()` after all plugins are built.  Shuts down WebGPU and
GLFW backends and destroys the ImGui context when the app exits.

### Options

| Field | Default | Effect |
|-------|---------|--------|
| `enable_docking` | `false` | Enables `ImGuiConfigFlags_DockingEnable` |
| `enable_viewports` | `false` | Enables `ImGuiConfigFlags_ViewportsEnable` and multi-viewport rendering |

```cpp
// Minimal setup
app.add_plugins(imgui::ImGuiPlugin{});

// With docking and multi-viewport
app.add_plugins(imgui::ImGuiPlugin{
    .enable_docking   = true,
    .enable_viewports = true,
});
// Equivalent builder form:
app.add_plugins(imgui::ImGuiPlugin{}.set_docking().set_viewports());
```

**Requirements:** `render::RenderPlugin` (and a GLFW window backend) must be
added before `ImGuiPlugin`.

Source: `epix_engine/render/examples/imgui_basic.cpp`

---

## Ctx

```cpp
namespace epix::imgui {
struct Ctx {};
}
```

A system parameter that, when added to a system's signature:

1. **Sets the ImGui context** on the calling worker thread before the system
   body runs (via `ImGui::SetCurrentContext`).
2. **Serialises** the system with all other systems that also take `Ctx`,
   preventing concurrent ImGui API calls across threads.

After `Ctx` is listed as a parameter, `ImGui::*` functions can be called
directly — no manual `ImGui::SetCurrentContext` needed.

`Ctx` takes a **write lock** on `ImGuiState`, so it conflicts with
`Res<ImGuiState>` or `ResMut<ImGuiState>` in the same system.

### Usage

```cpp
void my_ui(imgui::Ctx imgui) {
    ImGui::Begin("Window");
    ImGui::Text("Hello!");
    ImGui::End();
}

// Works in any main-world schedule:
app.add_systems(Update, into(my_ui));
```

```cpp
// Multiple Ctx systems run safely in parallel-compatible groups:
app.add_systems(Update, into(panel_a, panel_b)
    .set_names(std::array{"panel_a", "panel_b"}));
```

Source: `epix_engine/render/examples/imgui_basic.cpp`

---

## ImGuiState

```cpp
namespace epix::imgui {
struct ImGuiState {
    void* ctx             = nullptr;   // ImGuiContext*
    bool initialized      = false;
    bool frame_active     = false;
    bool enable_docking   = false;
    bool enable_viewports = false;
    std::shared_ptr<DrawDataSnapshot>                    draw_snapshot;
    std::shared_ptr<std::vector<ViewportDrawDataSnapshot>> viewport_snapshots;

    void activate() const;  // calls ImGui::SetCurrentContext(ctx)
};
}
```

`ImGuiState` is an internal resource managed by `ImGuiPlugin`.  It is
extracted to the render world each frame via `ExtractResourcePlugin<ImGuiState>`
so the render system can read the draw snapshot without racing with the main world.

Direct access is rarely needed.  Use `Ctx` instead of `Res<ImGuiState>` in
user systems.

### DrawDataSnapshot

An internal type that clones the ImGui `ImDrawData` at end-of-frame so the
render sub-app can consume it safely while the main world begins the next frame
(pipelined rendering).  Not part of the user-facing API.

---

## BeginFrameSet

```cpp
namespace epix::imgui {
inline struct BeginFrameSetT {} BeginFrameSet;
}
```

A system-set label for the `imgui_begin_frame` system inside `PreUpdate`.
Use it when you need to order your own `PreUpdate` systems relative to the
ImGui frame start:

```cpp
// Run my_setup before ImGui begins a new frame:
app.add_systems(PreUpdate,
    into(my_setup).before(imgui::BeginFrameSet));

// Run my_read after ImGui has processed GLFW input:
app.add_systems(PreUpdate,
    into(my_read).after(imgui::BeginFrameSet));
```

---

## Automatic Frame Lifecycle

The plugin registers all frame lifecycle systems automatically.  This table
shows the complete per-frame sequence:

| Schedule | System | Action |
|----------|--------|--------|
| `PreUpdate` (BeginFrameSet) | `imgui_begin_frame` | Lazy-inits GLFW backend; calls `ImGui_ImplGlfw_NewFrame()` + `ImGui::NewFrame()` |
| `Update` (user systems) | — | User systems call `ImGui::*` via `Ctx` |
| Post-every-schedule | `imgui_consume_input` | Checks `WantCaptureKeyboard` / `WantCaptureMouse`; advances event heads and sets bypass flags on `ButtonInput` so engine input systems do not see events ImGui consumed |
| `Last` | `imgui_end_frame` | Calls `ImGui::EndFrame()` + `ImGui::Render()`; snapshots draw data into `ImGuiState` |
| Render sub-app (after `RenderSet::Render`) | `imgui_render` | Lazy-inits WebGPU backend; reconstructs draw data; submits a render pass with `LoadOp::eLoad` to composite ImGui on top of the scene |

### Input Consumption Detail

`imgui_consume_input` runs as a **post-system** (after every schedule in the
main world).  When ImGui sets `WantCaptureKeyboard`:
- Key events are advanced past (consumed) in `Events<KeyInput>`
- `ButtonInput<KeyCode>` bypass flags are set so `just_pressed` / `pressed` / `just_released` return empty

When ImGui sets `WantCaptureMouse`:
- Mouse button and scroll events are similarly consumed
- `ButtonInput<MouseButton>` bypass flags are set

This means engine game code does not need to check `ImGui::GetIO().WantCapture*`
manually.

### Multi-Viewport Rendering

When `enable_viewports = true`, each ImGui platform viewport (a floating
ImGui window dragged outside the main window) gets its own `wgpu::Surface`
created from the GLFW window ImGui manages.  The render system creates,
reconfigures, and destroys these surfaces automatically as viewports appear
and disappear.  The framebuffer size of each viewport is captured on the main
thread (GLFW is not thread-safe) and passed to the render thread via
`ViewportDrawDataSnapshot`.

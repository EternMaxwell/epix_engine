# epix.render.imgui — Quick Reference

Integrates [Dear ImGui](https://github.com/ocornut/imgui) into the Epix engine with
zero boilerplate.  Adding `ImGuiPlugin` wires up the GLFW and WebGPU backends,
manages the frame lifecycle, handles input consumption, and renders ImGui output
on top of the scene — all automatically.  User systems only need to call ImGui
API functions; no manual context management required.

```cpp
#include <imgui.h>
import epix.render.imgui;
```

---

## Core Parts

| Name | Kind | Description |
|------|------|-------------|
| [`ImGuiPlugin`](imgui-plugin.md#imguiplugin) | Plugin | Wires up GLFW + WebGPU backends and all frame lifecycle systems |
| [`Ctx`](imgui-plugin.md#ctx) | System param | Activates the ImGui context on the calling thread; enables ImGui API calls |
| [`ImGuiState`](imgui-plugin.md#imguistate) | Resource | Holds the ImGui context pointer and per-frame draw snapshot |
| [`BeginFrameSet`](imgui-plugin.md#beginframeset) | Schedule set | Set label for the `imgui_begin_frame` system in `PreUpdate` |

---

## Quick Guide

### 1. Add the plugin

```cpp
app.add_plugins(render::RenderPlugin{})
   .add_plugins(imgui::ImGuiPlugin{});

// Optional: enable ImGui docking and multi-viewport:
app.add_plugins(imgui::ImGuiPlugin{
    .enable_docking   = true,
    .enable_viewports = true,
});
```

**Requirements:** `RenderPlugin` (and a window backend such as `GLFWRenderPlugin`)
must be added before `ImGuiPlugin`.

### 2. Draw ImGui in any system

Add `imgui::Ctx` to the system parameter list.  That is the only change needed —
the context is set automatically before the system body runs.

```cpp
void my_ui_system(imgui::Ctx imgui) {
    ImGui::Begin("Hello from Engine");
    ImGui::Text("ImGui is working in a multithreaded ECS!");
    if (ImGui::Button("Click me")) {
        // handle click
    }
    ImGui::End();
}

// Register in any main-world schedule:
app.add_systems(Update, into(my_ui_system));
```

The `imgui::Ctx` parameter also serialises the system with all other ImGui-using
systems, preventing concurrent ImGui API calls.

### 3. What the plugin does automatically

| Lifecycle stage | What happens |
|-----------------|--------------|
| Plugin `build()` | Creates `ImGuiContext`, sets `NavEnableKeyboard`, stores `ImGuiState` in the world |
| `PreUpdate` (`BeginFrameSet`) | Lazy-inits GLFW backend on first available primary window; calls `ImGui_ImplGlfw_NewFrame()` + `ImGui::NewFrame()` |
| After every schedule | `imgui_consume_input` checks `WantCaptureKeyboard` / `WantCaptureMouse`; blocks key/mouse events from reaching other systems when ImGui owns them |
| `Last` | Calls `ImGui::EndFrame()` + `ImGui::Render()`; snapshots draw data for pipelined rendering |
| Render sub-app (after `RenderSet::Render`) | `imgui_render` lazy-inits the WebGPU backend; reconstructs draw data and submits a render pass that composites ImGui on top of the scene |
| Plugin `finalize()` | Shuts down WebGPU + GLFW backends; destroys the ImGui context |

Source: `epix_engine/render/examples/imgui_basic.cpp`

# Render Plugin, Schedule & Extraction

## RenderPlugin

`epix::render::RenderPlugin` is the entry point for the WebGPU rendering subsystem.
It creates a dedicated render sub-app, initialises the WebGPU instance/adapter/device
and registers all core rendering systems.

```cpp
struct RenderPlugin {
    int validation = 0;             // 0 = none, 1 = nvrhi, 2 = Vulkan layers
    RenderPlugin& set_validation(int level = 0) noexcept;
    void build(core::App&);
    void finalize(core::App&) noexcept;
};
```

`build()` is called immediately when the plugin is added; `finalize()` runs after
all plugins have been added (during `App::run()` startup).  The render sub-app is
identified by the [`Render`](#render-schedule) sentinel.

**Requirements:** A window backend must have registered an `AnonymousSurface`
resource **before** `RenderPlugin::finalize()` runs, so the adapter/device can
be created against a real surface.

### Usage

```cpp
app.add_plugins(glfw::GLFWRenderPlugin{})   // registers AnonymousSurface
   .add_plugins(render::RenderPlugin{});     // default: no validation

// With Vulkan validation layers:
app.add_plugins(render::RenderPlugin{}.set_validation(2));
```

Source: `epix_engine/render/render/modules/render.cppm`

---

## AnonymousSurface

```cpp
namespace epix::render {
struct AnonymousSurface {
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
};
}
```

A temporary resource inserted by window backends so `RenderPlugin` can request
a WebGPU adapter (which requires a surface on some backends).  The functor
creates and returns a surface from a `wgpu::Instance`.  It is released after
adapter/device creation.

---

## Render Schedule

```cpp
namespace epix::render {
constexpr RenderT Render;
}
```

`Render` is a schedule-sentinel value used to identify the render sub-app when
calling `App::sub_app_mut()` or scheduling systems:

```cpp
auto& render_app = app.sub_app_mut(render::Render);
render_app.add_systems(render::Render,
    into(my_system).in_set(render::RenderSet::Queue));
```

---

## RenderSet

System-set labels used inside the render schedule.  The execution order is:

```
PostExtract
  └── (parallel) PrepareAssets
  └── ManageViews → Queue → PhaseSort → Prepare → Render → Cleanup
                                        Prepare:
                                          PrepareResources → PrepareFlush → PrepareSets
```

```cpp
enum class RenderSet {
    PostExtract,      // runs immediately after data is extracted
    PrepareAssets,    // upload meshes, textures, etc. (parallel with ManageViews)
    ManageViews,      // update camera views
    Queue,            // queue draw calls and phase items
    PhaseSort,        // sort phase items
    Prepare,          // top-level prepare stage
    PrepareResources, // create GPU buffers/textures
    PrepareFlush,     // flush pending uploads
    PrepareSets,      // create bind groups / pipeline layouts
    Render,           // execute the render graph
    Cleanup,          // post-render cleanup
};
```

---

## ExtractSchedule

```cpp
namespace epix::render {
struct ExtractScheduleT {} ExtractSchedule;
}
```

Systems added to `ExtractSchedule` run between the main world and the render
world each frame.  They copy (extract) relevant state from the main world
into the render world using the `Extract<...>` system parameter.

---

## ExtractResourcePlugin<T>

A convenience plugin that copies a single copyable resource from the main
world into the render world each frame.

```cpp
export template <std::copyable T>
struct ExtractResourcePlugin {
    void build(App& app);
};
```

**Usage:**

```cpp
// Extract MyConfig from the main world into the render world:
app.add_plugins(render::ExtractResourcePlugin<MyConfig>{});
```

The extraction system checks whether `T` is modified each frame (via change
detection) and only writes it to the render world resource when it has changed.

---

## CustomRendered

```cpp
export struct CustomRendered {};
```

A marker component.  Entities with `CustomRendered` are skipped by standard
render pipelines.  Use it for entities that are entirely managed by custom
rendering code.

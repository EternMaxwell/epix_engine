# Render plugin, schedules, and extraction

`RenderPlugin` initializes WebGPU and creates the render sub-app. Add it after
the task, window, platform-window, platform-render, and transform plugins used
by the application.

```cpp
import epix.ecs;
import epix.app;
import epix.task;
import epix.window;
import epix.transform;
import epix.render;
import epix.glfw.core;
import epix.glfw.render;

App app = App::create();
app.add_plugins(TaskPoolPlugin{})
   .add_plugins(window::WindowPlugin{})
   .add_plugins(glfw::GLFWPlugin{})
   .add_plugins(glfw::GLFWRenderPlugin{})
   .add_plugins(transform::TransformPlugin{})
   .add_plugins(render::RenderPlugin{}.set_validation(2));
```

## `RenderPlugin`

Select the validation level before adding the plugin: `0` disables validation,
`1` enables NVRHI validation, and `2` enables Vulkan validation layers. Plugins
are deduplicated by type, so adding a second `RenderPlugin` does not reconfigure
an already attached instance.

## What attachment creates

`RenderPlugin::attach()`:

- creates the `Render` sub-app and its `ExtractSchedule` and `Render` schedule;
- creates the WebGPU instance, high-performance Vulkan adapter, device, queue,
  limits, and default image sampler;
- inserts shared `PipelineServer` state into the main and render worlds;
- installs window rendering, image GPU extraction, shader, camera, and view
  plugins;
- inserts the root `RenderGraph` in the render world; and
- installs graph execution, device polling, extraction flush, and per-frame
  render-world cleanup systems.

## `AnonymousSurface`

An `AnonymousSurface` resource is optional. A backend may provide its
`create_surface(instance)` callback to constrain initial adapter selection;
otherwise adapter creation proceeds with a null temporary surface. It is
removed after adapter selection. Actual window swapchains use the per-window
`render::window::SurfaceCreation` component supplied by GLFW/SFML render
integration.

```cpp
struct AnonymousSurface {
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
};
```

The WebGPU instance, adapter, device, queue, limits, and default sampler are
available in both worlds. Retrieve the render sub-app with:

```cpp
App& render_app = app.sub_app_mut(render::Render);
```

## Render schedule

The window runner coordinates the worlds once per frame:

1. main-world schedules update application state;
2. the render sub-app runs `ExtractSchedule` while `Extract<T>` can access the
   main world;
3. the render sub-app runs its `Render` schedule;
4. the render graph records and submits command buffers; and
5. temporary render entities are cleared after cleanup.

`Render` is both the render sub-app label and its schedule label. Add
render-world systems to that sub-app, not to the main app:

```cpp
app.sub_app_mut(render::Render).add_systems(
    render::Render,
    into(queue_sprites).in_set(render::RenderSet::Queue));
```

## `RenderSet`

The main chain is:

```text
PostExtract -> ManageViews -> Queue -> PhaseSort -> Prepare -> Render -> Cleanup
```

Asset preparation runs on a parallel branch that rejoins at `Prepare`:

```text
PostExtract -> PrepareAssets -> Prepare
```

Inside `Prepare`:

```text
PrepareResources -> PrepareFlush -> PrepareSets
```

| Set | Use it for |
| --- | --- |
| `PostExtract` | apply or normalize freshly extracted state |
| `PrepareAssets` | turn extracted CPU assets into GPU resources |
| `ManageViews` | acquire targets and prepare camera/view state |
| `Queue` | add phase items and select pipelines |
| `PhaseSort` | sort phase items |
| `PrepareResources` | allocate/upload buffers and textures |
| `PrepareFlush` | flush deferred resource preparation |
| `PrepareSets` | create bind groups and final pipeline state |
| `Render` | execute the render graph or direct render work |
| `Cleanup` | release frame-local state |

The render schedule uses direct deferred-command application. `ExtractSchedule`
initially ignores deferred application; a `PostExtract` system applies its
queued commands in the render world before later sets run.

## `ExtractSchedule`

Use `Extract<T>` in a system installed on `ExtractSchedule`:

```cpp
void extract_scene(
    Commands commands,
    Extract<Query<Item<Entity, const Transform&, const Sprite&>>> source,
    ResMut<ExtractedSprites> destination)
{
    destination->clear();
    for (auto&& [entity, transform, sprite] : source.iter()) {
        destination->push(entity, transform, sprite);
    }
}

render_app.add_systems(render::ExtractSchedule, into(extract_scene));
```

Wrapped parameters access the source world; unwrapped parameters access the
render world. Deferred wrapped parameters such as `Extract<Commands>` are not
allowed. The source reference is frame-scoped and must not escape extraction.
Mutable extraction parameters are supported for deliberate move/take
operations, as used by render assets.

## `ExtractResourcePlugin`

For a copyable resource, `ExtractResourcePlugin<T>` provides the standard
pattern:

```cpp
app.add_plugins(render::ExtractResourcePlugin<SceneSettings>{});
```

It inserts the render-world resource on first extraction and copies it again
only when the source resource is modified. Add it after `RenderPlugin`, because
its attachment expects the `Render` sub-app to exist.

## `CustomRendered`

`CustomRendered` is a marker for entities handled entirely by a custom
renderer. Standard higher-level pipelines can use it as an exclusion filter.

## Window-facing rendering resources

`RenderPlugin` installs `render::window::WindowRenderPlugin`. The platform
render plugin adds `SurfaceCreation` to each backend window. Extraction creates
`ExtractedWindows`; render preparation creates/configures `WindowSurfaces`,
acquires each swapchain texture and view, and presentation occurs immediately
after the render graph submission and before `RenderSystems::Cleanup`.

Normally these resources are internal to camera/view preparation.

The extension-facing shapes are:

```cpp
using SurfaceCreation = std::function<wgpu::Surface(const wgpu::Instance&)>;

struct ExtractedWindow {
    Entity entity;
    SurfaceCreation create_surface;
    int physical_width;
    int physical_height;
    window::PresentMode present_mode;
    window::CompositeAlphaMode alpha_mode;
    wgpu::TextureView swapchain_texture_view;
    wgpu::SurfaceTexture swapchain_texture;
    wgpu::TextureFormat swapchain_texture_format;
    bool size_changed;
    bool present_mode_changed;
};

struct ExtractedWindows {
    std::optional<Entity> primary;
    std::unordered_map<Entity, ExtractedWindow> windows;
};

struct WindowRenderPlugin {
    void attach(App&);
};
```

`extract_windows`, `prepare_windows`, and the plugin are exported for custom
window/render integrations. Surface cache types and direct creation/present
systems are implementation support; ordinary render code should consume
`ExtractedWindows` or camera `ViewTarget` instead.

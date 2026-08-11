# epix.render — Quick Reference

The `epix.render` module is the WebGPU-backed rendering subsystem.  It sets up a
dedicated *render sub-app*, extracts data from the main world each frame, drives
a data-flow **render graph**, manages pipelines asynchronously, and provides the
camera / projection / view machinery used by higher-level rendering modules.

```cpp
import epix.ecs;
import epix.app;
import epix.assets;
import epix.shader;
import epix.render;
```

Application types and built-in schedules live in `epix.app`; ECS parameters,
queries, schedules, and `World` live in `epix.ecs`. The render module builds on
both foundation modules.

---

## Core Parts

| Name | Kind | Description |
|------|------|-------------|
| [`RenderPlugin`](render-plugin.md#renderplugin) | Plugin | Initializes WebGPU; hosts the render sub-app |
| [`Render`](render-plugin.md#render-schedule) | Schedule sentinel | Identifies the render sub-app schedule |
| [`RenderSet`](render-plugin.md#renderset) | Enum | System-set labels inside the render schedule |
| [`ExtractSchedule`](render-plugin.md#extractschedule) | Schedule sentinel | Phase that copies data from the main world |
| [`ExtractResourcePlugin<T>`](render-plugin.md#extractresourceplugin) | Plugin template | Copies a copyable resource into the render world |
| [`CustomRendered`](render-plugin.md#customrendered) | Component | Marks entities handled by custom pipelines |
| [`AnonymousSurface`](render-plugin.md#anonymoussurface) | Resource | Surface creation functor for adapter/device init |
| [`WindowRenderPlugin`](render-plugin.md#window-facing-rendering-resources) | Plugin | Extracts windows, owns surfaces, acquires and presents swapchain images |
| [`SurfaceCreation`](render-plugin.md#window-facing-rendering-resources) | Component alias | Platform callback that creates a WebGPU surface for one window |
| [`ExtractedWindows`](render-plugin.md#window-facing-rendering-resources) | Resource | Render-world window and current swapchain snapshots |
| [`RenderGraph`](render-graph.md#rendergraph) | Struct | Directed-acyclic graph that drives per-frame rendering |
| [`Node`](render-graph.md#node) | Base class | Override to implement custom render node logic |
| [`GraphContext`](render-graph.md#graphcontext) | Struct | Slot I/O and sub-graph dispatch during node execution |
| [`RenderContext`](render-graph.md#rendercontext) | Struct | GPU device + command encoder during node execution |
| [`NodeLabel`](render-graph.md#labels) | Label | Identifies a node in a render graph |
| [`GraphLabel`](render-graph.md#labels) | Label | Identifies a sub-graph |
| [`SlotType`](render-graph.md#slots) | Enum | Buffer / Texture / Sampler / Entity |
| [`SlotInfo`](render-graph.md#slots) | Struct | Named slot descriptor |
| [`SlotValue`](render-graph.md#slots) | Struct | Type-erased slot payload |
| [`Camera`](camera-view.md#camera) | Component | Viewport, order, render target, clear color |
| [`Projection`](camera-view.md#projection) | Component | Orthographic or perspective projection |
| [`OrthographicProjection`](camera-view.md#orthographicprojection) | Struct | Ortho camera with ScalingMode |
| [`PerspectiveProjection`](camera-view.md#perspectiveprojection) | Struct | Perspective camera (fov/aspect/near/far) |
| [`ScalingMode`](camera-view.md#scalingmode) | Struct | Factory-built ortho scaling strategy |
| [`RenderTarget`](camera-view.md#rendertarget) | Variant | Texture or window reference |
| [`RenderLayer`](camera-view.md#renderlayer) | Struct | Bit-vector layer set with complement support |
| [`ClearColor`](camera-view.md#clearcolor) | Resource | Global RGBA clear color |
| [`ClearColorConfig`](camera-view.md#clearcolorconfig) | Struct | None / Global / Custom per-camera config |
| [`CameraBundle`](camera-view.md#camerabundle) | Bundle | Spawns a fully configured camera entity |
| [`CameraRenderGraph`](camera-view.md#camerarendergraph) | Component | Which render graph this camera drives |
| [`CameraPlugin`](camera-view.md#cameraplugin) | Plugin | Updates/extracts cameras and installs the camera driver node |
| [`CameraProjectionPlugin<T>`](camera-view.md#camera-projection-extension-point) | Plugin template | Updates a custom camera projection type |
| [`ViewPlugin`](camera-view.md#viewplugin) | Plugin | Registers view extraction and depth systems |
| [`ViewTarget`](camera-view.md#viewtarget) | Component | Swapchain texture view + format for a camera |
| [`ViewDepth`](camera-view.md#viewdepth) | Component | Depth texture + view for a camera |
| [`ViewUniform`](camera-view.md#viewuniform) | Struct | Projection + view matrices for shaders |
| [`ViewBindGroup`](camera-view.md#viewbindgroup) | Component | Bind group exposing the ViewUniform |
| [`BindViewUniform<Slot>`](camera-view.md#bindviewuniformslot) | Render command | Binds view uniform at a specified slot |
| [`VisibleEntities`](camera-view.md#visibleentities) | Component | Entities visible to a camera |
| [`PipelineServer`](pipeline.md#pipelineserver) | Resource | Async render/compute pipeline cache |
| `CachedPipelineId` | ID | Stable key returned when a pipeline is queued |
| [`RenderPipelineDescriptor`](pipeline.md#descriptors) | Struct | Builder for render pipeline creation |
| [`ComputePipelineDescriptor`](pipeline.md#descriptors) | Struct | Builder for compute pipeline creation |
| [`VertexState`](pipeline.md#vertexstate) | Struct | Vertex shader + buffer layouts |
| [`FragmentState`](pipeline.md#fragmentstate) | Struct | Fragment shader + color targets |
| [`RenderPipeline`](pipeline.md#rendercompute-pipeline) | Struct | Created render pipeline with unique ID |
| [`ComputePipeline`](pipeline.md#rendercompute-pipeline) | Struct | Created compute pipeline with unique ID |
| [`RenderAsset<T>`](assets.md#renderassett) | Trait struct | Specialise to make T a GPU render asset |
| [`RenderAssets<T>`](assets.md#renderassetst) | Resource | Storage for processed GPU assets |
| [`ExtractAssetPlugin<T>`](assets.md#extractassetplugint) | Plugin template | Wires extract + process pipeline for asset T |
| [`GPUImage`](assets.md#gpuimage) | Struct | Texture + view + sampler for an Image |
| [`RenderPhase<P>`](render-phase.md#renderphasep) | Component | Sorted draw list + execution loop |
| [`DrawFunctions<P>`](render-phase.md#drawfunctionsp) | Resource | Thread-safe draw function registry |
| [`PhaseItem`](render-phase.md#concepts) | Concept | Required interface for phase items |
| [`DrawFunction<P>`](render-phase.md#drawfunctionp) | Base class | Abstract type-erased draw function |
| [`RenderCommand`](render-phase.md#rendercommand-concept) | Concept | Render command template constraint |
| [`SetItemPipeline<P>`](render-phase.md#setitempipelinep) | Render command | Binds the cached pipeline for a phase item |
| [`app_add_render_commands<P, R...>()`](render-phase.md#app_add_render_commands) | Free function | Registers a command chain as a draw function |
| [`sort_phase_items<P>`](render-phase.md#sort_phase_itemsp) | System template | Sorts all RenderPhase<P> components |
| [`Core2dPlugin`](core-2d.md) | Plugin | Built-in opaque, transparent, and UI 2D graph/phases |
| [`ScreenshotPlugin`](screenshot.md) | Plugin | Window or texture capture into `Image` assets |

---

## Quick Guide

### 1. Add a complete renderer stack

```cpp
app.add_plugins(TaskPoolPlugin{})
   .add_plugins(window::WindowPlugin{})
   .add_plugins(glfw::GLFWPlugin{})
   .add_plugins(glfw::GLFWRenderPlugin{})
   .add_plugins(transform::TransformPlugin{})
   .add_plugins(render::RenderPlugin{}.set_validation(2));
```

Use validation level `0` for normal runs. Add `RenderPlugin` only once; plugin
types are deduplicated. The platform render plugin supplies each window's
`SurfaceCreation` callback, while `RenderPlugin` creates the render sub-app and
installs window/image/shader/camera/view rendering.

### 2. Spawn a camera

```cpp
// In a Startup system:
constexpr struct MyGraph {} my_graph;
cmd.spawn(render::camera::CameraBundle::with_render_graph(my_graph));
```

`CameraBundle` defaults to orthographic projection targeting the primary window.

### 3. Build a render graph node

```cpp
struct MyNode : render::graph::Node {
    void run(render::graph::GraphContext& graph_ctx,
             render::graph::RenderContext& render_ctx,
             const ecs::World& world) override {
        auto view = graph_ctx.view_entity();
        auto& encoder = render_ctx.command_encoder();
        // ... issue draw calls ...
    }
};

constexpr struct MyNodeLabel {} my_node;

// Register a camera-driven sub-graph during plugin setup:
auto& render_app = app.sub_app_mut(render::Render);
auto& root = render_app.world_mut().resource_mut<render::RenderGraph>();
render::RenderGraph camera_graph;
camera_graph.add_node(my_node, MyNode{});
root.add_sub_graph(my_graph, std::move(camera_graph)).value();
```

### 4. Queue a render pipeline

```cpp
// PipelineServer is shared between the main and render worlds.
void setup(Res<render::PipelineServer> server,
           Res<MyShaderHandles> shader_handles,
           ResMut<MyPipelineIds> pipeline_ids) {
    if (pipeline_ids->main) return;
    pipeline_ids->main = server->queue_render_pipeline(
        render::RenderPipelineDescriptor{}
            .set_label("my_pipeline")
            .set_vertex(render::VertexState{}.set_shader(shader_handles->vertex))
            .set_fragment(render::FragmentState{}.set_shader(shader_handles->fragment)
                              .add_target(wgpu::ColorTargetState{})));
}
```

### 5. Define and register a draw function

```cpp
// Implement PhaseItem and Draw, then register in the render world:
auto& render_app = app.sub_app_mut(render::Render);
render_app.world_mut().init_resource<render::phase::DrawFunctions<MyPhaseItem>>();
auto& draw_fns = render_app.world_mut().resource_mut<render::phase::DrawFunctions<MyPhaseItem>>();
auto id = draw_fns.add<MyDrawFunction>(render_app.world_mut());
```

Or use the render-command chain helper:

```cpp
render::phase::app_add_render_commands<
    MyPhaseItem,
    render::phase::SetItemPipeline,
    MySetBindGroup,
    MyDraw
>(render_app);
```

### 6. Extract an asset to GPU

```cpp
// Specialize RenderAsset<T> and add the plugin:
app.add_plugins(render::ExtractAssetPlugin<MyMesh>{});
// Access processed assets in render systems:
void use_mesh(Res<render::RenderAssets<MyMesh>> meshes, Res<MeshHandle> handle) {
    if (const auto* mesh = meshes->try_get(handle->id())) { /* bind it */ }
}
```

## Where to go next

- [Plugin, schedule, extraction, and window flow](render-plugin.md)
- [Camera and view setup](camera-view.md)
- [Render graph nodes, slots, and sub-graphs](render-graph.md)
- [Pipeline descriptors and asynchronous readiness](pipeline.md)
- [Render assets and GPU image extraction](assets.md)
- [Phases, draw functions, and render-command chains](render-phase.md)


# epix.render — Quick Reference

The `epix.render` module is the WebGPU-backed rendering subsystem.  It sets up a
dedicated *render sub-app*, extracts data from the main world each frame, drives
a data-flow **render graph**, manages pipelines asynchronously, and provides the
camera / projection / view machinery used by higher-level rendering modules.

```cpp
import epix.render;
```

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
| [`ViewPlugin`](camera-view.md#viewplugin) | Plugin | Registers view extraction and depth systems |
| [`ViewTarget`](camera-view.md#viewtarget) | Component | Swapchain texture view + format for a camera |
| [`ViewDepth`](camera-view.md#viewdepth) | Component | Depth texture + view for a camera |
| [`ViewUniform`](camera-view.md#viewuniform) | Struct | Projection + view matrices for shaders |
| [`ViewBindGroup`](camera-view.md#viewbindgroup) | Component | Bind group exposing the ViewUniform |
| [`BindViewUniform<Slot>`](camera-view.md#bindviewuniform) | Render command | Binds view uniform at a specified slot |
| [`VisibleEntities`](camera-view.md#visibleentities) | Component | Entities visible to a camera |
| [`PipelineServer`](pipeline.md#pipelineserver) | Resource | Async render/compute pipeline cache |
| [`RenderPipelineDescriptor`](pipeline.md#descriptors) | Struct | Builder for render pipeline creation |
| [`ComputePipelineDescriptor`](pipeline.md#descriptors) | Struct | Builder for compute pipeline creation |
| [`VertexState`](pipeline.md#vertex-fragment-state) | Struct | Vertex shader + buffer layouts |
| [`FragmentState`](pipeline.md#vertex-fragment-state) | Struct | Fragment shader + color targets |
| [`RenderPipeline`](pipeline.md#render-compute-pipeline) | Struct | Created render pipeline with unique ID |
| [`ComputePipeline`](pipeline.md#render-compute-pipeline) | Struct | Created compute pipeline with unique ID |
| [`RenderAsset<T>`](assets.md#renderasset) | Trait struct | Specialise to make T a GPU render asset |
| [`RenderAssets<T>`](assets.md#renderassets) | Resource | Storage for processed GPU assets |
| [`ExtractAssetPlugin<T>`](assets.md#extractassetplugin) | Plugin template | Wires extract + process pipeline for asset T |
| [`GPUImage`](assets.md#gpuimage) | Struct | Texture + view + sampler for an Image |
| [`RenderPhase<P>`](render-phase.md#renderphase) | Component | Sorted draw list + execution loop |
| [`DrawFunctions<P>`](render-phase.md#drawfunctions) | Resource | Thread-safe draw function registry |
| [`PhaseItem`](render-phase.md#concepts) | Concept | Required interface for phase items |
| [`DrawFunction<P>`](render-phase.md#drawfunction) | Base class | Abstract type-erased draw function |
| [`RenderCommand`](render-phase.md#rendercommand-concept) | Concept | Render command template constraint |
| [`SetItemPipeline<P>`](render-phase.md#setitempipeline) | Render command | Binds the cached pipeline for a phase item |
| [`app_add_render_commands<P, R...>()`](render-phase.md#app_add_render_commands) | Free function | Registers a command chain as a draw function |
| [`sort_phase_items<P>`](render-phase.md#sort_phase_items) | System template | Sorts all RenderPhase<P> components |

---

## Quick Guide

### 1. Add the plugin

```cpp
// Requires a window backend (e.g. GLFWRenderPlugin) that registers SurfaceCreation.
app.add_plugins(render::RenderPlugin{});
// Optional: enable Vulkan validation layers
app.add_plugins(render::RenderPlugin{}.set_validation(2));
```

### 2. Spawn a camera

```cpp
// In a Startup system:
auto [cmd] = params.get();
constexpr struct MyGraph {} my_graph;
cmd.spawn(render::camera::CameraBundle::with_render_graph(my_graph));
```

`CameraBundle` defaults to orthographic projection targeting the primary window.

### 3. Build a render graph node

```cpp
struct MyNode : render::graph::Node {
    std::vector<render::graph::SlotInfo> inputs() override {
        return {{ "view", render::graph::SlotType::Entity }};
    }
    void run(render::graph::GraphContext& graph_ctx,
             render::graph::RenderContext& render_ctx,
             const World& world) override {
        auto view = graph_ctx.get_input_entity("view").value();
        auto& encoder = render_ctx.command_encoder();
        // ... issue draw calls ...
    }
};

// Register in render sub-app during build:
auto& render_app = app.sub_app_mut(render::Render);
auto& graph = render_app.world_mut().resource_mut<render::RenderGraph>();
graph.add_node(my_graph, MyNode{});
```

### 4. Queue a render pipeline

```cpp
// In a render-world system (RenderSet::Queue or PrepareResources):
void setup(ResMut<render::PipelineServer> server,
           Res<assets::Assets<shader::Shader>> shaders) {
    auto vs = shaders.get(vs_handle);
    auto pipeline_id = server->queue_render_pipeline(
        render::RenderPipelineDescriptor{}
            .set_label("my_pipeline")
            .set_vertex(render::VertexState{}.set_shader(vs_handle))
            .set_fragment(render::FragmentState{}.set_shader(fs_handle)
                              .add_target(wgpu::ColorTargetState{})));
}
```

### 5. Define and register a draw function

```cpp
// Implement PhaseItem and Draw, then register:
auto& draw_fns = world.resource_mut<render::phase::DrawFunctions<MyPhaseItem>>();
auto id = draw_fns.add<MyDrawFunction>(world);
```

Or use the render-command chain helper:

```cpp
render::phase::app_add_render_commands<MyPhaseItem, SetItemPipeline, MySetBindGroup, MyDraw>(app);
```

### 6. Extract an asset to GPU

```cpp
// Specialize RenderAsset<T> and add the plugin:
app.add_plugins(render::ExtractAssetPlugin<MyMesh>{});
// Access processed assets in render systems:
auto& gpu_meshes = world.resource<render::RenderAssets<MyMesh>>();
auto* mesh = gpu_meshes.try_get(handle.id());
```


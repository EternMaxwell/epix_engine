# Pipeline & PipelineServer

`PipelineServer` is the central, thread-safe cache for WebGPU render and
compute pipelines.  Pipelines are **queued** asynchronously and compiled in a
background thread pool; the render graph reads them back when they are ready.

---

## Descriptors

Use the builder-pattern descriptor structs to describe a pipeline before
queuing it.

### VertexState

```cpp
struct VertexState {
    assets::Handle<shader::Shader>          shader;
    std::optional<std::string>              entry_point;
    std::vector<wgpu::VertexBufferLayout>   buffers;

    auto&& set_shader(assets::Handle<shader::Shader>);
    auto&& set_entry_point(std::string);
    auto&& set_buffer(uint32_t index, wgpu::VertexBufferLayout);
    auto&& set_buffers(range auto&& buffers);
};
```

### FragmentState

```cpp
struct FragmentState {
    assets::Handle<shader::Shader>         shader;
    std::optional<std::string>             entry_point;
    std::vector<wgpu::ColorTargetState>    targets;

    auto&& set_shader(assets::Handle<shader::Shader>);
    auto&& set_entry_point(std::string);
    auto&& add_target(wgpu::ColorTargetState);
};
```

### RenderPipelineDescriptor

```cpp
struct RenderPipelineDescriptor {
    std::string                          label;
    std::vector<wgpu::BindGroupLayout>   layouts;
    VertexState                          vertex;
    wgpu::PrimitiveState                 primitive;
    std::optional<wgpu::DepthStencilState> depth_stencil;
    wgpu::MultisampleState               multisample;
    std::optional<FragmentState>         fragment;

    auto&& set_label(std::string);
    auto&& add_layout(wgpu::BindGroupLayout);
    auto&& set_layouts(range auto&&);
    auto&& set_vertex(VertexState);
    auto&& set_primitive(wgpu::PrimitiveState);
    auto&& set_depth_stencil(wgpu::DepthStencilState);
    auto&& set_multisample(wgpu::MultisampleState);
    auto&& set_fragment(FragmentState);
};
```

### ComputePipelineDescriptor

```cpp
struct ComputePipelineDescriptor {
    std::string                          label;
    std::vector<wgpu::BindGroupLayout>   layouts;
    assets::Handle<shader::Shader>       shader;
    std::optional<std::string>           entry_point;

    auto&& set_label(std::string);
    auto&& add_layout(wgpu::BindGroupLayout);
    auto&& set_layouts(range auto&&);
    auto&& set_shader(assets::Handle<shader::Shader>);
    auto&& set_entry_point(std::string);
};
```

---

## Render/Compute Pipeline

Created pipelines are wrapped in:

```cpp
struct RenderPipeline {
    RenderPipelineId          id()       const noexcept;
    const wgpu::RenderPipeline& pipeline() const noexcept;
  private:
    RenderPipeline(wgpu::RenderPipeline); // created only by PipelineServer
};

struct ComputePipeline {
    ComputePipelineId           id()       const noexcept;
    const wgpu::ComputePipeline& pipeline() const noexcept;
};
```

`RenderPipelineId` and `ComputePipelineId` are atomic-increment uint64 IDs.
The type alias `CachedPipelineId` (aliased from `shader::CachedPipelineId`)
is the stable key used to retrieve a pipeline from the cache.

---

## PipelineServer

```cpp
struct PipelineServer {
    // Queue a pipeline for async compilation
    CachedPipelineId queue_render_pipeline(RenderPipelineDescriptor) const;
    CachedPipelineId queue_compute_pipeline(ComputePipelineDescriptor) const;

    // Retrieve a compiled pipeline
    std::expected<std::reference_wrapper<const RenderPipeline>, GetPipelineError>
        get_render_pipeline(CachedPipelineId id) const noexcept;
    std::expected<std::reference_wrapper<const ComputePipeline>, GetPipelineError>
        get_compute_pipeline(CachedPipelineId id) const noexcept;

    // Inspect state
    optional<ref<const CachedPipelineState>>
        get_pipeline_state(CachedPipelineId id) const noexcept;
    optional<ref<const RenderPipelineDescriptor>>
        get_render_pipeline_descriptor(CachedPipelineId id) const noexcept;
    optional<ref<const ComputePipelineDescriptor>>
        get_compute_pipeline_descriptor(CachedPipelineId id) const noexcept;
};
```

`PipelineServer` is value-copyable; all copies share the same underlying
`shared_ptr<PipelineServerData>`.  It exists both in the main app (for queuing
pipelines) and in the render app (for executing them).

### Pipeline Lifecycle

```
CachedPipelineState =
    | PipelineStateQueued                    // waiting in the queue
    | PipelineStateCreating                  // future in flight on thread pool
    | PipelineStateRecoverableShaderError    // shader not loaded yet, will retry
    | Pipeline                               // ready (variant<RenderPipeline, ComputePipeline>)
    | PipelineServerError                    // permanent error
```

### Usage

```cpp
// In a render-world system during Prepare / PrepareResources:
void my_system(ResMut<render::PipelineServer> server, ...) {
    if (!my_pipeline_id) {
        my_pipeline_id = server->queue_render_pipeline(
            render::RenderPipelineDescriptor{}
                .set_label("my_pipeline")
                .set_vertex(render::VertexState{}.set_shader(vs_handle))
                .set_fragment(render::FragmentState{}.set_shader(fs_handle)
                                  .add_target(wgpu::ColorTargetState{})));
    }
}

// In a draw command or Render system:
auto pipeline = server->get_render_pipeline(my_pipeline_id);
if (pipeline) {
    encoder.setPipeline(pipeline->get().pipeline());
}
```

### Retrieval Errors

```cpp
using GetPipelineError = variant<GetPipelineNotReady,     // still compiling
                                  GetPipelineInvalidId,    // ID out of range
                                  PipelineServerError>;    // creation failed
using PipelineServerError = variant<PipelineError,         // CreationFailure
                                     shader::ShaderCacheError>;
```

---

## LayoutCache

```cpp
struct LayoutCache {
    wgpu::PipelineLayout get(const wgpu::Device& device,
                             ranges::range auto&& bind_group_layouts);
};
```

Deduplicates `wgpu::PipelineLayout` objects by their constituent
`wgpu::BindGroupLayout` IDs.  Managed internally by `PipelineServer`; not
normally accessed directly.

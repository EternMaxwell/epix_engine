# Camera & View

Cameras drive rendering by specifying *what* to render (render target, clear
color, layer mask), *how* (projection, viewport), and *which render graph* to
use.  Internally, camera data is extracted into the render world each frame and
processed by `ViewPlugin`.

All camera types live in `epix::render::camera`.  Extracted/view-side types
live in `epix::render::view`.

---

## Camera

```cpp
namespace epix::render::camera {
struct Camera {
    std::optional<Viewport>   viewport;           // sub-region, null = full target
    std::ptrdiff_t            order       = 0;    // higher renders on top
    bool                      active      = true;
    RenderTarget              render_target = RenderTarget::from_primary();
    ComputedCameraValues      computed;           // updated by camera systems
    ClearColorConfig          clear_color = ClearColorConfig::global();

    glm::uvec2 get_viewport_size()   const noexcept;
    glm::uvec2 get_target_size()     const noexcept;
    glm::uvec2 get_viewport_origin() const noexcept;
};
}
```

### Viewport

```cpp
struct Viewport {
    glm::uvec2 pos;                              // top-left in pixels
    glm::uvec2 size;                             // dimensions in pixels
    std::pair<float, float> depth_range{0.0f, 1.0f};
};
```

---

## RenderTarget

```cpp
struct RenderTarget : std::variant<wgpu::Texture, WindowRef> {
    static RenderTarget from_texture(wgpu::Texture);
    static RenderTarget from_primary() noexcept;      // primary window
    static RenderTarget from_window(Entity) noexcept; // specific window entity
};
```

`WindowRef` holds `bool primary` and `Entity window_entity`.

---

## ClearColor

Global clear color resource.  Placed in the world by the user (or the render
plugin default); cameras can override it per-camera via `ClearColorConfig`.

```cpp
struct ClearColor : public glm::vec4 { ... };
```

## ClearColorConfig

```cpp
struct ClearColorConfig {
    enum class Type { None, Global, Custom } type = Type::Default;
    ClearColor clear_color{0, 0, 0, 1};

    static ClearColorConfig none()   noexcept;  // don't clear
    static ClearColorConfig def()    noexcept;  // use world ClearColor
    static ClearColorConfig global() noexcept;  // alias for def()
    static ClearColorConfig custom(const glm::vec4& color) noexcept;
};
```

---

## RenderLayer

```cpp
struct RenderLayer {
    utils::bit_vector bits;
    bool inverted = false;

    static RenderLayer all()   noexcept;        // matches every layer
    static RenderLayer none()  noexcept;        // matches nothing
    static RenderLayer layer(std::size_t n);    // matches exactly layer n
    static RenderLayer layers(range auto&& ns); // matches any layer in set
    static RenderLayer all_except(range auto&& ns);

    bool contains(std::size_t n) const noexcept;
    bool intersects(const RenderLayer& other) const noexcept;
};
```

Cameras use `RenderLayer::all()` by default; entities use layer 0.
`intersects()` governs camera-vs-entity visibility.

---

## Projection

```cpp
struct Projection {
    std::variant<OrthographicProjection, PerspectiveProjection> projection;

    static Projection orthographic(const OrthographicProjection& = {});
    static Projection perspective(const PerspectiveProjection& = {});

    glm::mat4 get_projection_matrix() const;
    std::array<glm::vec3, 8> get_frustum_corners() const;
    float get_near() / get_far() const;
    void  set_near(float) / set_far(float);
    void  update(float width, float height);

    optional<OrthographicProjection*> as_orthographic();
    optional<PerspectiveProjection*>  as_perspective();
};
```

### OrthographicProjection

```cpp
struct OrthographicProjection {
    float near_plane        = -1000.0f;
    float far_plane         =  1000.0f;
    ScalingMode scaling_mode = ScalingMode::window_size(1.0f);
    float scale              = 1.0f;
    glm::vec2 viewport_origin = {0.5f, 0.5f};
    struct { float left, right, bottom, top; } rect;

    void update(float width, float height); // recomputes rect
    glm::mat4 get_projection_matrix() const;
};
```

### PerspectiveProjection

```cpp
struct PerspectiveProjection {
    float fov          = glm::radians(45.0f);
    float aspect_ratio = 1.0f;
    float near_plane   = 0.1f;
    float far_plane    = 1000.0f;

    void update(float width, float height);  // recomputes aspect
    glm::mat4 get_projection_matrix() const;
};
```

---

## ScalingMode

Factory-pattern struct controlling how an `OrthographicProjection` maps world
units to screen pixels.

| Factory | Behaviour |
|---------|-----------|
| `fixed(w, h)` | Fixed world-space rectangle regardless of window size |
| `window_size(ppu)` | 1 world unit = `ppu` pixels (default `ppu = 1`) |
| `auto_min(min_w, min_h)` | At least min_w × min_h world units visible |
| `auto_max(max_w, max_h)` | At most max_w × max_h world units visible |
| `fixed_vertical(v)` | Fixed vertical world extent; horizontal adapts |
| `fixed_horizontal(h)` | Fixed horizontal world extent; vertical adapts |

`on_fixed()`, `on_window_size()`, … allow conditional mutation by mode.

---

## CameraRenderGraph

```cpp
struct CameraRenderGraph : public graph::GraphLabel {
    using graph::GraphLabel::GraphLabel;
};
```

A `GraphLabel` that identifies which named sub-graph in the render graph this
camera drives.  Set on the `CameraBundle` and extracted each frame.

---

## CameraBundle

```cpp
struct CameraBundle {
    Camera               camera;
    Projection           projection;
    CameraRenderGraph    render_graph;
    transform::Transform transform;
    view::VisibleEntities visible;
    RenderLayer          render_layer = RenderLayer::all();

    static CameraBundle with_render_graph(const CameraRenderGraph& graph);
};
```

Spawns a camera entity with 6 components.  The `CameraBundle::write()` Bundle
specialisation writes all 6 into ECS storage atomically.

```cpp
// Startup system:
cmd.spawn(render::camera::CameraBundle::with_render_graph(my_graph));
// Perspective camera:
auto bundle = render::camera::CameraBundle::with_render_graph(my_graph);
bundle.projection = render::camera::Projection::perspective();
cmd.spawn(std::move(bundle));
```

Source: `epix_engine/render/examples/render_plugin.cpp`

---

## ViewPlugin

```cpp
struct ViewPlugin {
    void build(App& app);
};
```

Registered automatically by `RenderPlugin`.  Adds systems for:
- Extracting camera data (`extract_cameras`)
- Preparing `ViewTarget` (swapchain texture view) per camera entity
- Creating / reusing `ViewDepth` textures (`ViewDepthCache`)
- Building `ViewUniform` + `ViewBindGroup` each frame

---

## View-Side Components

### VisibleEntities

```cpp
struct VisibleEntities {
    std::vector<Entity> entities;
};
```

Updated by culling systems; contains entity IDs visible to this camera.

### ExtractedView

```cpp
struct ExtractedView {
    glm::mat4            projection;
    transform::GlobalTransform transform;
    glm::uvec2           viewport_size;
    glm::uvec2           viewport_origin;
};
```

Extracted from each camera into the render world.

### ViewTarget

```cpp
struct ViewTarget {
    wgpu::TextureView texture_view;
    wgpu::TextureFormat format;
};
```

The swapchain (or off-screen) texture view a render graph writes to.

### ViewDepth

```cpp
struct ViewDepth {
    wgpu::Texture     texture;
    wgpu::TextureView depth_view;
};
```

Depth buffer for a camera view.  Cached by viewport size in `ViewDepthCache`.

---

## ViewUniform

```cpp
struct ViewUniform {
    glm::mat4 projection;
    glm::mat4 view;        // inverse camera transform
};
```

Uploaded to a GPU uniform buffer each frame.

## ViewBindGroup

```cpp
struct ViewBindGroup {
    wgpu::BindGroup bind_group;
};
```

Exposes the `ViewUniform` to shaders at bind group index 0 (by default).

## ViewUniformBindingLayout

```cpp
struct ViewUniformBindingLayout {
    wgpu::BindGroupLayout layout;
    ViewUniformBindingLayout(World& world);
};
```

A world-resource holding the bind group layout for `ViewUniform`.

---

## BindViewUniform<Slot>

```cpp
template <std::size_t Slot>
struct BindViewUniform {
    template <render::phase::PhaseItem P>
    struct Command { /* RenderCommand */ };
};
```

A render command template that binds the `ViewBindGroup` at bind group slot
`Slot`.  Use it as part of a `RenderCommandSequence`:

```cpp
render::phase::app_add_render_commands<
    MyPhaseItem,
    render::view::BindViewUniform<0>::Command,   // binds view UBO at slot 0
    SetItemPipeline,
    MyDrawCommand
>(app);
```

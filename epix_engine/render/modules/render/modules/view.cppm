module;

export module epix.render:view;

import epix.transform;
import epix.core;
import webgpu;

import :window;
import :graph;
import :render_phase;

using namespace core;

namespace render::camera {
export struct Viewport {
    glm::uvec2 pos;
    glm::uvec2 size;
    std::pair<float, float> depth_range{0.0f, 1.0f};
};
export struct WindowRef {
    bool primary = true;
    Entity window_entity;  // Used if primary is false
};
export struct RenderTarget : std::variant<wgpu::Texture, WindowRef> {
    using std::variant<wgpu::Texture, WindowRef>::variant;
    static RenderTarget from_texture(wgpu::Texture texture) { return RenderTarget(std::move(texture)); }
    static RenderTarget from_primary() { return RenderTarget(WindowRef{true}); }
    static RenderTarget from_window(Entity window_entity) { return RenderTarget(WindowRef{false, window_entity}); }
    std::optional<RenderTarget> normalize(std::optional<Entity> primary) const;
};
struct ComputedCameraValues {
    glm::mat4 projection;
    glm::uvec2 target_size;
    std::optional<glm::uvec2> old_viewport_size;
};
export struct ClearColor : public glm::vec4 {
    using glm::vec4::vec4;
    ClearColor(const glm::vec4& v) : glm::vec4(v) {}
    glm::vec4 to_vec4() const { return glm::vec4(*this); }
};
export struct ClearColorConfig {
    enum class Type {
        None,    // don't clear
        Global,  // use world's clear color resource
        Default = Global,
        Custom,  // use custom clear color
    } type = Type::Default;
    ClearColor clear_color{0.0f, 0.0f, 0.0f, 1.0f};

    static ClearColorConfig none() { return ClearColorConfig{Type::None}; }
    static ClearColorConfig def() { return ClearColorConfig{Type::Default}; }
    static ClearColorConfig global() { return ClearColorConfig{Type::Global}; }
    static ClearColorConfig custom(const glm::vec4& color) { return ClearColorConfig{Type::Custom, color}; }
};
export struct Camera {
    /// @brief The camera's viewport within the render target.
    std::optional<Viewport> viewport;
    /// @brief Cameras with higher order are rendered on top of cameras with
    /// lower order.
    std::ptrdiff_t order = 0;
    /// @brief Whether this camera is active and should be used for rendering.
    /// If false, the camera will be ignored.
    bool active = true;

    RenderTarget render_target = RenderTarget::from_primary();
    ComputedCameraValues computed;
    ClearColorConfig clear_color = ClearColorConfig::global();

    glm::uvec2 get_viewport_size() const {
        return viewport.transform([](const Viewport& vp) { return vp.size; }).value_or(computed.target_size);
    }
    glm::uvec2 get_target_size() const { return computed.target_size; }
    glm::uvec2 get_viewport_origin() const {
        return viewport.transform([](const Viewport& vp) { return vp.pos; }).value_or(glm::uvec2(0, 0));
    }
};
export struct ScalingMode {
   private:
    enum class Mode {
        Fixed,
        WindowSize,
        AutoMin,
        AutoMax,
        FixedVertical,
        FixedHorizontal,
    } mode;
    union {
        struct {
            float width;
            float height;
        } _fixed;
        struct {
            float pixels_per_unit;
        } _window_size;
        struct {
            float min_width;
            float min_height;
        } _auto_min;
        struct {
            float max_width;
            float max_height;
        } _auto_max;
        struct {
            float vertical;
        } _fixed_vertical;
        struct {
            float horizontal;
        } _fixed_horizontal;
    };

   public:
    ScalingMode() : mode(Mode::WindowSize) { _window_size.pixels_per_unit = 1.0f; }
    static ScalingMode fixed(float width, float height) {
        ScalingMode mode;
        mode.mode          = Mode::Fixed;
        mode._fixed.width  = width;
        mode._fixed.height = height;
        return mode;
    }
    static ScalingMode window_size(float pixels_per_unit) {
        ScalingMode mode;
        mode.mode                         = Mode::WindowSize;
        mode._window_size.pixels_per_unit = pixels_per_unit;
        return mode;
    }
    static ScalingMode auto_min(float min_width, float min_height) {
        ScalingMode mode;
        mode.mode                 = Mode::AutoMin;
        mode._auto_min.min_width  = min_width;
        mode._auto_min.min_height = min_height;
        return mode;
    }
    static ScalingMode auto_max(float max_width, float max_height) {
        ScalingMode mode;
        mode.mode                 = Mode::AutoMax;
        mode._auto_max.max_width  = max_width;
        mode._auto_max.max_height = max_height;
        return mode;
    }
    static ScalingMode fixed_vertical(float vertical) {
        ScalingMode mode;
        mode.mode                     = Mode::FixedVertical;
        mode._fixed_vertical.vertical = vertical;
        return mode;
    }
    static ScalingMode fixed_horizontal(float horizontal) {
        ScalingMode mode;
        mode.mode                         = Mode::FixedHorizontal;
        mode._fixed_horizontal.horizontal = horizontal;
        return mode;
    }

    template <std::invocable<float&, float&> Func>
    ScalingMode& on_fixed(Func&& func) {
        if (mode == Mode::Fixed) {
            func(_fixed.width, _fixed.height);
        }
        return *this;
    }
    template <std::invocable<float&> Func>
    ScalingMode& on_window_size(Func&& func) {
        if (mode == Mode::WindowSize) {
            func(_window_size.pixels_per_unit);
        }
        return *this;
    }
    template <std::invocable<float&, float&> Func>
    ScalingMode& on_auto_min(Func&& func) {
        if (mode == Mode::AutoMin) {
            func(_auto_min.min_width, _auto_min.min_height);
        }
        return *this;
    }
    template <std::invocable<float&, float&> Func>
    ScalingMode& on_auto_max(Func&& func) {
        if (mode == Mode::AutoMax) {
            func(_auto_max.max_width, _auto_max.max_height);
        }
        return *this;
    }
    template <std::invocable<float&> Func>
    ScalingMode& on_fixed_vertical(Func&& func) {
        if (mode == Mode::FixedVertical) {
            func(_fixed_vertical.vertical);
        }
        return *this;
    }
    template <std::invocable<float&> Func>
    ScalingMode& on_fixed_horizontal(Func&& func) {
        if (mode == Mode::FixedHorizontal) {
            func(_fixed_horizontal.horizontal);
        }
        return *this;
    }
};
export struct OrthographicProjection {
    float near_plane          = -1000.0f;  // Near clipping plane
    float far_plane           = 1000.0f;   // Far clipping plane
    ScalingMode scaling_mode  = ScalingMode::window_size(1.0f);
    float scale               = 1.0f;                   // Additional scale factor
    glm::vec2 viewport_origin = glm::vec2(0.5f, 0.5f);  // Viewport origin (0 to 1)
    struct {
        float left   = -1.0f;
        float right  = 1.0f;
        float bottom = -1.0f;
        float top    = 1.0f;
    } rect;

    void update(float width, float height);
    float get_far() const { return far_plane; }
    float get_near() const { return near_plane; }
    void set_far(float far_plane) { this->far_plane = far_plane; }
    void set_near(float near_plane) { this->near_plane = near_plane; }
    glm::mat4 get_projection_matrix() const {
        return glm::gtc::orthoLH(rect.left, rect.right, rect.bottom, rect.top, near_plane, far_plane);
    }
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return {glm::vec3(rect.left, rect.bottom, near_plane), glm::vec3(rect.right, rect.bottom, near_plane),
                glm::vec3(rect.right, rect.top, near_plane),   glm::vec3(rect.left, rect.top, near_plane),
                glm::vec3(rect.left, rect.bottom, far_plane),  glm::vec3(rect.right, rect.bottom, far_plane),
                glm::vec3(rect.right, rect.top, far_plane),    glm::vec3(rect.left, rect.top, far_plane)};
    }
};
export struct PerspectiveProjection {
    float fov          = glm::radians(45.0f);  // Field of view in radians
    float aspect_ratio = 1.0f;                 // Aspect ratio (width / height)
    float near_plane   = 0.1f;                 // Near clipping plane
    float far_plane    = 1000.0f;              // Far clipping plane

    void update(float width, float height) { aspect_ratio = width / height; }
    float get_far() const { return far_plane; }
    float get_near() const { return near_plane; }
    void set_far(float far_plane) { this->far_plane = far_plane; }
    void set_near(float near_plane) { this->near_plane = near_plane; }
    glm::mat4 get_projection_matrix() const {
        return glm::gtc::perspectiveLH(fov, aspect_ratio, near_plane, far_plane);
    }
    std::array<glm::vec3, 8> get_frustum_corners() const {
        float tan_half_fov = glm::tan(fov / 2.0f);
        float near_height  = near_plane * tan_half_fov;
        float near_width   = near_height * aspect_ratio;
        float far_height   = far_plane * tan_half_fov;
        float far_width    = far_height * aspect_ratio;

        return {glm::vec3(-near_width, -near_height, near_plane), glm::vec3(near_width, -near_height, near_plane),
                glm::vec3(near_width, near_height, near_plane),   glm::vec3(-near_width, near_height, near_plane),
                glm::vec3(-far_width, -far_height, far_plane),    glm::vec3(far_width, -far_height, far_plane),
                glm::vec3(far_width, far_height, far_plane),      glm::vec3(-far_width, far_height, far_plane)};
    }
};
template <typename T>
concept CameraProjection = requires(T t) {
    { t.get_projection_matrix() } -> std::convertible_to<glm::mat4>;
    { t.get_frustum_corners() } -> std::convertible_to<std::array<glm::vec3, 8>>;
    { t.get_far() } -> std::convertible_to<float>;
    { t.get_near() } -> std::convertible_to<float>;
    { t.set_far(std::declval<float>()) };
    { t.set_near(std::declval<float>()) };
    { t.update(std::declval<float>(), std::declval<float>()) };
};

export struct Projection {
    std::variant<OrthographicProjection, PerspectiveProjection> projection;

    Projection() : projection(OrthographicProjection{}) {}
    Projection(const OrthographicProjection& ortho) : projection(ortho) {}
    Projection(const PerspectiveProjection& perspective) : projection(perspective) {}

    static Projection orthographic(const OrthographicProjection& ortho = {}) { return Projection(ortho); }

    static Projection perspective(const PerspectiveProjection& perspective = {}) { return Projection(perspective); }

    glm::mat4 get_projection_matrix() const {
        return std::visit([](const auto& proj) { return proj.get_projection_matrix(); }, projection);
    }
    float get_far() const {
        return std::visit([](const auto& proj) { return proj.get_far(); }, projection);
    }
    float get_near() const {
        return std::visit([](const auto& proj) { return proj.get_near(); }, projection);
    }
    void set_far(float far_plane) {
        std::visit([far_plane](auto& proj) { proj.set_far(far_plane); }, projection);
    }
    void set_near(float near_plane) {
        std::visit([near_plane](auto& proj) { proj.set_near(near_plane); }, projection);
    }
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return std::visit([](const auto& proj) { return proj.get_frustum_corners(); }, projection);
    }
    void update(float width, float height) {
        std::visit([width, height](auto& proj) { proj.update(width, height); }, projection);
    }

    std::optional<OrthographicProjection*> as_orthographic() {
        if (auto ptr = std::get_if<OrthographicProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
    std::optional<const OrthographicProjection*> as_orthographic() const {
        if (auto ptr = std::get_if<OrthographicProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
    std::optional<PerspectiveProjection*> as_perspective() {
        if (auto ptr = std::get_if<PerspectiveProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
    std::optional<const PerspectiveProjection*> as_perspective() const {
        if (auto ptr = std::get_if<PerspectiveProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
};
static_assert(CameraProjection<OrthographicProjection>);
static_assert(CameraProjection<PerspectiveProjection>);
static_assert(CameraProjection<Projection>);

// --- Camera Systems --- //

export enum class CameraUpdateSystems {
    CameraUpdateSystem = 0,
};

template <CameraProjection ProjType>
void camera_system(
    Query<Item<Mut<Camera>, Mut<ProjType>>> query,      // camera and projection query
    Query<Item<const ::window::Window&>> window_query,  // window query
    Query<Item<const ::window::Window&>, With<::window::PrimaryWindow>> primary_window_query  // primary window query
) {
    for (auto&& [camera, proj] : query.iter()) {
        // in the body we want to update the stored target size,
        // update the projection if needed.

        std::optional<glm::uvec2> viewport_size =
            camera.get_mut().viewport.transform([](const Viewport& vp) { return glm::uvec2(vp.size); });

        glm::uvec2 target_size;
        std::visit(assets::visitor{
                       [&](const WindowRef& window_ref) {
                           if (window_ref.primary) {
                               // primary window
                               if (auto primary = primary_window_query.single()) {
                                   auto&& [win] = *primary;
                                   target_size  = glm::uvec2(win.size.first, win.size.second);
                               } else {
                                   // no primary window, use 0x0 as invalid
                                   target_size = glm::uvec2(0, 0);
                               }
                           } else {
                               // specific window
                               if (auto opt_win = window_query.get(window_ref.window_entity)) {
                                   auto [win]  = *opt_win;
                                   target_size = glm::uvec2(win.size.first, win.size.second);
                               } else {
                                   // window not found, use 0x0 as invalid
                                   target_size = glm::uvec2(0, 0);
                               }
                           }
                       },
                       [&](const wgpu::Texture& texture) {
                           // texture target
                           if (texture) {
                               target_size = glm::uvec2(texture.getWidth(), texture.getHeight());
                           } else {
                               // null texture, use 0x0 as invalid
                               target_size = glm::uvec2(0, 0);
                           }
                       },
                   },
                   camera.get().render_target);

        // only update projection if logical viewport size changed
        std::optional<glm::uvec2> new_viewport_size =
            viewport_size
                .and_then([&](const glm::uvec2& vp_size) -> std::optional<glm::uvec2> {
                    // not equal to old viewport size
                    if (camera.get().computed.old_viewport_size.has_value() &&
                        *camera.get().computed.old_viewport_size == vp_size) {
                        return std::nullopt;
                    } else {
                        return vp_size;
                    }
                })
                .or_else([&]() -> std::optional<glm::uvec2> {
                    // no viewport, use full target size
                    if (camera.get().computed.old_viewport_size.has_value() &&
                        *camera.get().computed.old_viewport_size == target_size) {
                        return std::nullopt;
                    } else if (camera.get().computed.target_size == target_size) {
                        return std::nullopt;
                    } else {
                        return target_size;
                    }
                });

        camera.get_mut().computed.target_size       = target_size;
        camera.get_mut().computed.old_viewport_size = viewport_size;

        auto new_size = camera.get().get_viewport_size();
        proj.get_mut().update((float)new_size.x, (float)new_size.y);

        camera.get_mut().computed.projection = proj.get().get_projection_matrix();
    }
}

export template <CameraProjection ProjType>
struct CameraProjectionPlugin {
    void build(App& app) {
        app.add_systems(PostUpdate, into(camera_system<ProjType>).in_set(CameraUpdateSystems::CameraUpdateSystem));
        app.add_systems(PostStartup, into(camera_system<ProjType>).in_set(CameraUpdateSystems::CameraUpdateSystem));
    }
};

export struct CameraRenderGraph : public graph::GraphLabel {
    using graph::GraphLabel::GraphLabel;
};

export struct ExtractedCamera {
    // this render target is a normalized one, which means if it is a WindowRef and is primary, the entity field will
    // point to the actual primary window entity.
    RenderTarget render_target;
    glm::uvec2 viewport_size;
    glm::uvec2 target_size;
    std::optional<Viewport> viewport;
    CameraRenderGraph render_graph;
    ptrdiff_t order;
    std::optional<ClearColor> clear_color;
};
}  // namespace render::camera
namespace render::view {
export struct ExtractedView {
    glm::mat4 projection;
    transform::GlobalTransform transform;
    glm::uvec2 viewport_size;
    glm::uvec2 viewport_origin;
};
export struct VisibleEntities {
    std::vector<Entity> entities;
};
export struct ViewTarget {
    wgpu::TextureView texture_view;
};
export struct ViewDepth {
    wgpu::Texture texture;
    wgpu::TextureView depth_view;
};
export struct UVec2Hash {
    std::size_t operator()(const glm::uvec2& v) const noexcept {
        std::size_t h = (static_cast<std::size_t>(v.x) << 32) | v.y;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }
};
export struct ViewDepthCache {
    std::unordered_map<glm::uvec2, wgpu::Texture, UVec2Hash> cache;
};

export struct ViewPlugin {
    void build(App& app);
};

void prepare_view_target(Query<Item<Entity, const camera::ExtractedCamera&, const ExtractedView&>> views,
                         Commands cmd,
                         Res<window::ExtractedWindows> extracted_windows);
void create_view_depth(Query<Item<Entity, const ExtractedView&>> views,
                       Res<wgpu::Device> device,
                       Res<wgpu::Queue> queue,
                       ResMut<ViewDepthCache> depth_cache,
                       Commands cmd);

export struct ViewUniform {
    glm::mat4 projection;
    glm::mat4 view;
};
struct UniformBuffer {
    wgpu::Buffer buffer;
};
export struct ViewBindGroup {
    wgpu::BindGroup bind_group;
};
struct ViewUniformBindingLayout {
    wgpu::BindGroupLayout layout;
    ViewUniformBindingLayout(World& world)
        : layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor().setEntries(std::array{
                  wgpu::BindGroupLayoutEntry()
                      .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
                      .setBinding(0)
                      .setBuffer(wgpu::BufferBindingLayout()
                                     .setType(wgpu::BufferBindingType::eUniform)
                                     .setHasDynamicOffset(false)
                                     .setMinBindingSize(sizeof(ViewUniform))),
              }))) {}
};
export template <size_t Slot>
struct BindViewUniform {
    template <render::phase::PhaseItem P>
    struct Command {
        std::unordered_map<wgpu::raw::Buffer, wgpu::BindGroup> uniform_set_cache;
        void prepare(World&) { uniform_set_cache.clear(); }
        bool render(const P&,
                    Item<const UniformBuffer&> view_uniform,
                    std::optional<Item<>> entity_item,
                    ParamSet<Res<wgpu::Device>, Res<ViewUniformBindingLayout>> params,
                    const wgpu::RenderPassEncoder& encoder) {
            auto&& [device, uniform_layout] = params.get();
            auto&& [ub]                     = *view_uniform;
            return true;
        }
    };
};
}  // namespace render::view
namespace render::camera {
export void extract_cameras(
    Commands cmd,
    Res<ClearColor> global_clear_color,
    Extract<Query<
        Item<const Camera&, const CameraRenderGraph&, const transform::GlobalTransform&, const view::VisibleEntities&>>>
        cameras,
    Extract<Query<Entity, With<::window::PrimaryWindow, ::window::Window>>> primary_window);

export inline constexpr struct CameraDriverNodeLabelT {
} CameraDriverNodeLabel;

struct CameraPlugin {
    void build(App& app);
};
export struct CameraBundle {
    Camera camera;
    Projection projection;
    CameraRenderGraph render_graph;
    transform::Transform transform;
    view::VisibleEntities visible;

    CameraBundle(const CameraRenderGraph& graph) : render_graph(graph) {}

    static CameraBundle with_render_graph(const CameraRenderGraph& graph) {
        CameraBundle bundle(graph);
        return bundle;
    }
};
}  // namespace render::camera
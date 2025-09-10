#pragma once

#include <epix/image.h>
#include <epix/transform/transform.h>
#include <epix/window.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "common.h"
#include "graph.h"
#include "view.h"
#include "window.h"

namespace epix::render::camera {
struct Viewport {
    glm::uvec2 pos;
    glm::uvec2 size;
    std::pair<float, float> depth_range{0.0f, 1.0f};
};
struct WindowRef {
    bool primary = true;
    Entity window_entity;  // Used if primary is false
};
struct RenderTarget : std::variant<nvrhi::TextureHandle, WindowRef> {
    using std::variant<nvrhi::TextureHandle, WindowRef>::variant;
    static RenderTarget from_texture(nvrhi::TextureHandle texture) { return RenderTarget(texture); }
    static RenderTarget from_primary() { return RenderTarget(WindowRef{true}); }
    static RenderTarget from_window(Entity window_entity) { return RenderTarget(WindowRef{false, window_entity}); }
    EPIX_API std::optional<RenderTarget> normalize(std::optional<Entity> primary) const;
};
struct ComputedCameraValues {
    glm::mat4 projection;
    glm::uvec2 target_size;
    std::optional<glm::uvec2> old_viewport_size;
};
struct ClearColor : public glm::vec4 {
    using glm::vec4::vec4;
    ClearColor(const glm::vec4& v) : glm::vec4(v) {}
    glm::vec4 to_vec4() const { return glm::vec4(*this); }
};
struct ClearColorConfig {
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
struct Camera {
    /// @brief The camera's viewport within the render target.
    std::optional<Viewport> viewport;
    /// @brief Cameras with higher order are rendered on top of cameras with
    /// lower order.
    ptrdiff_t order = 0;
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
struct ScalingMode {
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
struct OrthographicProjection {
    float near_plane          = 0.0f;     // Near clipping plane
    float far_plane           = 1000.0f;  // Far clipping plane
    ScalingMode scaling_mode  = ScalingMode::window_size(1.0f);
    float scale               = 1.0f;                   // Additional scale factor
    glm::vec2 viewport_origin = glm::vec2(0.5f, 0.5f);  // Viewport origin (0 to 1)
    struct {
        float left   = -1.0f;
        float right  = 1.0f;
        float bottom = -1.0f;
        float top    = 1.0f;
    } rect;

    EPIX_API void update(float width, float height);
    float get_far() const { return far_plane; }
    float get_near() const { return near_plane; }
    void set_far(float far_plane) { this->far_plane = far_plane; }
    void set_near(float near_plane) { this->near_plane = near_plane; }
    glm::mat4 get_projection_matrix() const {
        return glm::orthoLH(rect.left, rect.right, rect.bottom, rect.top, near_plane, far_plane);
    }
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return {glm::vec3(rect.left, rect.bottom, near_plane), glm::vec3(rect.right, rect.bottom, near_plane),
                glm::vec3(rect.right, rect.top, near_plane),   glm::vec3(rect.left, rect.top, near_plane),
                glm::vec3(rect.left, rect.bottom, far_plane),  glm::vec3(rect.right, rect.bottom, far_plane),
                glm::vec3(rect.right, rect.top, far_plane),    glm::vec3(rect.left, rect.top, far_plane)};
    }
};
struct PerspectiveProjection {
    float fov          = glm::radians(45.0f);  // Field of view in radians
    float aspect_ratio = 1.0f;                 // Aspect ratio (width / height)
    float near_plane   = 0.1f;                 // Near clipping plane
    float far_plane    = 1000.0f;              // Far clipping plane

    void update(float width, float height) { aspect_ratio = width / height; }
    float get_far() const { return far_plane; }
    float get_near() const { return near_plane; }
    void set_far(float far_plane) { this->far_plane = far_plane; }
    void set_near(float near_plane) { this->near_plane = near_plane; }
    glm::mat4 get_projection_matrix() const { return glm::perspectiveLH(fov, aspect_ratio, near_plane, far_plane); }
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

struct Projection {
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
};
static_assert(CameraProjection<OrthographicProjection>);
static_assert(CameraProjection<PerspectiveProjection>);
static_assert(CameraProjection<Projection>);

// --- Camera Systems --- //

enum class CameraUpdateSystems {
    CameraUpdateSystem = 0,
};

template <CameraProjection ProjType>
void camera_system(
    Query<Get<Mut<Camera>, Mut<ProjType>>> query,   // camera and projection query
    Query<Get<epix::window::Window>> window_query,  // window query
    Query<Get<epix::window::Window>, With<epix::window::PrimaryWindow>> primary_window_query  // primary window query
) {
    for (auto&& [camera, proj] : query.iter()) {
        // in the body we want to update the stored target size,
        // update the projection if needed.

        std::optional<glm::uvec2> viewport_size =
            camera.viewport.transform([](const Viewport& vp) { return glm::uvec2(vp.size); });

        glm::uvec2 target_size;
        std::visit(epix::util::visitor{
                       [&](const WindowRef& window_ref) {
                           if (window_ref.primary) {
                               // primary window
                               if (auto primary = primary_window_query.get_single()) {
                                   auto&& [win] = *primary;
                                   target_size  = glm::uvec2(win.size.first, win.size.second);
                               } else {
                                   // no primary window, use 0x0 as invalid
                                   target_size = glm::uvec2(0, 0);
                               }
                           } else {
                               // specific window
                               if (auto opt_win = window_query.try_get(window_ref.window_entity)) {
                                   auto [win]  = *opt_win;
                                   target_size = glm::uvec2(win.size.first, win.size.second);
                               } else {
                                   // window not found, use 0x0 as invalid
                                   target_size = glm::uvec2(0, 0);
                               }
                           }
                       },
                       [&](nvrhi::TextureHandle texture) {
                           // texture target
                           if (texture) {
                               target_size = glm::uvec2(texture->getDesc().width, texture->getDesc().height);
                           } else {
                               // null texture, use 0x0 as invalid
                               target_size = glm::uvec2(0, 0);
                           }
                       },
                   },
                   camera.render_target);

        // only update projection if logical viewport size changed
        std::optional<glm::uvec2> new_viewport_size =
            viewport_size
                .and_then([&](const glm::uvec2& vp_size) -> std::optional<glm::uvec2> {
                    // not equal to old viewport size
                    if (camera.computed.old_viewport_size.has_value() &&
                        *camera.computed.old_viewport_size == vp_size) {
                        return std::nullopt;
                    } else {
                        return vp_size;
                    }
                })
                .or_else([&]() -> std::optional<glm::uvec2> {
                    // no viewport, use full target size
                    if (camera.computed.old_viewport_size.has_value() &&
                        *camera.computed.old_viewport_size == target_size) {
                        return std::nullopt;
                    } else if (camera.computed.target_size == target_size) {
                        return std::nullopt;
                    } else {
                        return target_size;
                    }
                });
        if (new_viewport_size.has_value()) {
            // update projection
            proj.update((float)new_viewport_size->x, (float)new_viewport_size->y);
        }

        camera.computed.projection        = proj.get_projection_matrix();
        camera.computed.target_size       = target_size;
        camera.computed.old_viewport_size = viewport_size;
    }
}

template <CameraProjection ProjType>
struct CameraProjectionPlugin {
    void build(App& app) {
        app.add_systems(PostUpdate, into(camera_system<ProjType>).in_set(CameraUpdateSystems::CameraUpdateSystem));
        app.add_systems(PostStartup, into(camera_system<ProjType>).in_set(CameraUpdateSystems::CameraUpdateSystem));
    }
};

struct CameraRenderGraph : public graph::GraphLabel {
    using graph::GraphLabel::GraphLabel;
};

struct ExtractedCamera {
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

EPIX_API void extract_cameras(
    Commands& cmd,
    std::optional<Res<ClearColor>> global_clear_color,
    Extract<Query<Get<Camera, CameraRenderGraph, transform::GlobalTransform, view::VisibleEntities>>> cameras,
    Extract<Query<Get<Entity>, With<epix::window::PrimaryWindow, epix::window::Window>>> primary_window);

struct CameraDriverNode : graph::Node {
    EPIX_API void run(graph::GraphContext& graph, graph::RenderContext& render_ctx, app::World& world) override;
};

inline struct CameraDriverNodeLabelT {
} CameraDriverNodeLabel;

struct CameraPlugin {
    EPIX_API void build(App& app);
};
struct CameraBundle : public Bundle {
    Camera camera;
    Projection projection;
    CameraRenderGraph render_graph;
    transform::Transform transform;
    view::VisibleEntities visible;
    std::tuple<Camera, Projection, CameraRenderGraph, transform::Transform, view::VisibleEntities> unpack() {
        return {camera, projection, render_graph, transform, visible};
    }

    CameraBundle(const CameraRenderGraph& graph) : render_graph(graph) {}

    static CameraBundle with_render_graph(const CameraRenderGraph& graph) {
        CameraBundle bundle(graph);
        return bundle;
    }
};
static_assert(app::is_bundle<CameraBundle>);
}  // namespace epix::render::camera
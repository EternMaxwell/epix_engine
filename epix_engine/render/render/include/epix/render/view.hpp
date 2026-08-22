#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/transform.hpp>
#include <epix/utils.hpp>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/color_grading.hpp>
#include <epix/render/graph.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/texture_attachment.hpp>
#include <epix/render/window.hpp>

namespace epix::render::camera {
/** @brief Defines a sub-region of the render target for camera output. */
EPIX_EXPORT struct Viewport {
    /** @brief Top-left position of the viewport in pixels. */
    glm::uvec2 pos;
    /** @brief Size of the viewport in pixels. */
    glm::uvec2 size;
    /** @brief Depth range for the viewport (min, max). */
    std::pair<float, float> depth_range{0.0f, 1.0f};
};
/** @brief Reference to a window entity used as a render target.
 *
 * When `primary` is true, the primary window is used regardless of
 * `window_entity`. */
EPIX_EXPORT struct WindowRef {
    /** @brief Whether to target the primary window. */
    bool primary = true;
    /** @brief Window entity to target when primary is false. */
    epix::ecs::Entity window_entity;
};
/** @brief A render target that is either a GPU texture or a window
 * reference. */
struct RenderTargetId;  // forward decl; defined after RenderTarget
EPIX_EXPORT struct RenderTarget : std::variant<wgpu::Texture, WindowRef> {
    using std::variant<wgpu::Texture, WindowRef>::variant;
    static RenderTarget from_texture(wgpu::Texture texture) { return RenderTarget(std::move(texture)); }
    static RenderTarget from_primary() noexcept { return RenderTarget(WindowRef{true}); }
    static RenderTarget from_window(epix::ecs::Entity window_entity) noexcept {
        return RenderTarget(WindowRef{false, window_entity});
    }
    std::optional<RenderTarget> normalize(std::optional<epix::ecs::Entity> primary) const;
    /** @brief Stable identity for grouping/sorting by target (Bevy
     * NormalizedRenderTarget hashing). Textures are keyed by their raw handle,
     * windows by their entity uid. */
    RenderTargetId identity() const noexcept;
};

/** @brief Stable identity of a normalized render target, used to share one
 * output attachment per target and to group cameras by target. */
EPIX_EXPORT struct RenderTargetId {
    std::uint64_t value = 0;
    bool operator==(const RenderTargetId&) const noexcept = default;
};
/** @brief Hash for RenderTargetId. */
EPIX_EXPORT struct RenderTargetIdHash {
    std::size_t operator()(const RenderTargetId& id) const noexcept { return std::hash<std::uint64_t>{}(id.value); }
};
struct ComputedCameraValues {
    glm::mat4 projection;
    glm::uvec2 target_size;
    std::optional<glm::uvec2> old_viewport_size;
};
/** @brief RGBA clear color for render targets. */
EPIX_EXPORT struct ClearColor : public glm::vec4 {
    using glm::vec4::vec4;
    ClearColor(const glm::vec4& v) noexcept : glm::vec4(v) {}
    glm::vec4 to_vec4() const noexcept { return glm::vec4(*this); }
};
/** @brief Controls how the render target is cleared before rendering. */
EPIX_EXPORT struct ClearColorConfig {
    enum class Type {
        None,    // don't clear
        Global,  // use world's clear color resource
        Default = Global,
        Custom,  // use custom clear color
    } type = Type::Default;
    ClearColor clear_color{0.0f, 0.0f, 0.0f, 1.0f};

    static ClearColorConfig none() noexcept { return ClearColorConfig{Type::None}; }
    static ClearColorConfig def() noexcept { return ClearColorConfig{Type::Default}; }
    static ClearColorConfig global() noexcept { return ClearColorConfig{Type::Global}; }
    static ClearColorConfig custom(const glm::vec4& color) noexcept { return ClearColorConfig{Type::Custom, color}; }
};
/** @brief Identifies which render layers a camera renders or an entity belongs to.
 *
 * Backed by a dynamic bit vector.  When `inverted` is false (the default) the
 * component represents a finite set of active layer indices.  When `inverted`
 * is true it represents the complement — i.e. "all layers except those in
 * `bits`" — which allows expressing "render everything" without enumerating
 * every possible index.
 *
 * Matching rule (camera vs entity):
 *   `camera_layer.intersects(entity_layer)` returns true when there exists at
 *   least one layer index that is active in both.
 *
 * Default construction yields layer 0 (`bits = {0}`, `inverted = false`).
 */
EPIX_EXPORT struct RenderLayers {
    utils::bit_vector bits;
    bool inverted = false;

    /** @brief Default: entity is on layer 0. */
    RenderLayers() { bits.set(0); }

    /** @brief Internal constructor for factory methods. */
    RenderLayers(utils::bit_vector b, bool inv) : bits(std::move(b)), inverted(inv) {}

    /** @brief Matches all layers (camera default). */
    static RenderLayers all() { return RenderLayers(utils::bit_vector{}, true); }

    /** @brief Matches no layers. */
    static RenderLayers none() { return RenderLayers(utils::bit_vector{}, false); }

    /** @brief Matches exactly one layer. */
    static RenderLayers layer(std::size_t n) {
        utils::bit_vector b;
        b.set(n);
        return RenderLayers(std::move(b), false);
    }

    /** @brief Matches a set of layers (accepts any input range of `std::size_t`). */
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, std::size_t>
    static RenderLayers layers(R&& ns) {
        utils::bit_vector b;
        for (auto n : ns) b.set(static_cast<std::size_t>(n));
        return RenderLayers(std::move(b), false);
    }
    /** @brief Matches all layers except those in the range. */
    template <std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, std::size_t>
    static RenderLayers all_except(R&& ns) {
        utils::bit_vector b;
        for (auto n : ns) b.set(static_cast<std::size_t>(n));
        return RenderLayers(std::move(b), true);
    }

    /** @brief Returns true if layer index @p n is active on this RenderLayers. */
    bool contains(std::size_t n) const noexcept {
        bool in_bits = bits.contains(n);
        return inverted ? !in_bits : in_bits;
    }

    /** @brief Returns true if this and @p other share at least one active layer.
     *
     * Truth table for the four (inverted, inverted) combinations:
     *  - normal ∩ normal   → any shared set bit
     *  - normal ∩ inverted → any bit in self not excluded by other
     *  - inverted ∩ normal → any bit in other not excluded by self
     *  - inverted ∩ inverted → always true (both cover infinitely many layers
     *                           beyond their finite exclusion sets)
     */
    bool intersects(const RenderLayers& other) const noexcept {
        if (!inverted && !other.inverted) {
            return bits.intersect(other.bits);
        }
        if (!inverted && other.inverted) {
            // any bit in self.bits that is NOT excluded by other
            return bits.difference_count(other.bits) > 0;
        }
        if (inverted && !other.inverted) {
            // any bit in other.bits that is NOT excluded by self
            return other.bits.difference_count(bits) > 0;
        }
        // inverted ∩ inverted: both represent infinite sets — always overlap
        return true;
    }
};


/** @brief Camera component that controls viewport, render target, ordering,
 * and clear colour.
 *
 * Cameras with higher `order` render on top of those with lower order.
 * The computed projection and target size are updated automatically by
 * camera systems.
 */
EPIX_EXPORT struct Camera {
    /** @brief The camera's viewport within the render target. */
    std::optional<Viewport> viewport;
    /** @brief Cameras with higher order are rendered on top of cameras with
     * lower order. */
    std::ptrdiff_t order = 0;
    /** @brief Whether this camera is active and should be used for rendering. */
    bool active = true;
    /** @brief If true, the camera uses an intermediate HDR render texture
     * (Bevy Camera::hdr). */
    bool hdr = false;

    /** @brief The render target for this camera. */
    RenderTarget render_target = RenderTarget::from_primary();
    /** @brief Computed values updated by camera systems. */
    ComputedCameraValues computed;
    /** @brief Clear color configuration for this camera. */
    ClearColorConfig clear_color = ClearColorConfig::global();

    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator);

    /** @brief Get the effective viewport size, falling back to target size. */
    glm::uvec2 get_viewport_size() const noexcept {
        return viewport.transform([](const Viewport& vp) { return vp.size; }).value_or(computed.target_size);
    }
    /** @brief Get the render target's pixel dimensions. */
    glm::uvec2 get_target_size() const noexcept { return computed.target_size; }
    /** @brief Get the viewport origin, defaulting to (0, 0). */
    glm::uvec2 get_viewport_origin() const noexcept {
        return viewport.transform([](const Viewport& vp) { return vp.pos; }).value_or(glm::uvec2(0, 0));
    }
};
/** @brief Scaling mode controlling how an orthographic projection adapts to
 * the viewport size.
 *
 * Constructed via static factory methods (e.g. `fixed()`, `window_size()`,
 * `auto_min()`).
 */
EPIX_EXPORT struct ScalingMode {
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
    ScalingMode() noexcept : mode(Mode::WindowSize) { _window_size.pixels_per_unit = 1.0f; }
    static ScalingMode fixed(float width, float height) noexcept {
        ScalingMode mode;
        mode.mode          = Mode::Fixed;
        mode._fixed.width  = width;
        mode._fixed.height = height;
        return mode;
    }
    static ScalingMode window_size(float pixels_per_unit) noexcept {
        ScalingMode mode;
        mode.mode                         = Mode::WindowSize;
        mode._window_size.pixels_per_unit = pixels_per_unit;
        return mode;
    }
    static ScalingMode auto_min(float min_width, float min_height) noexcept {
        ScalingMode mode;
        mode.mode                 = Mode::AutoMin;
        mode._auto_min.min_width  = min_width;
        mode._auto_min.min_height = min_height;
        return mode;
    }
    static ScalingMode auto_max(float max_width, float max_height) noexcept {
        ScalingMode mode;
        mode.mode                 = Mode::AutoMax;
        mode._auto_max.max_width  = max_width;
        mode._auto_max.max_height = max_height;
        return mode;
    }
    static ScalingMode fixed_vertical(float vertical) noexcept {
        ScalingMode mode;
        mode.mode                     = Mode::FixedVertical;
        mode._fixed_vertical.vertical = vertical;
        return mode;
    }
    static ScalingMode fixed_horizontal(float horizontal) noexcept {
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
/** @brief Orthographic camera projection with configurable scaling, near/far
 * planes, and viewport origin. */
EPIX_EXPORT struct OrthographicProjection {
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

    /** @brief Update the projection for new viewport dimensions. */
    void update(float width, float height);
    /** @brief Get the far clipping plane distance. */
    float get_far() const { return far_plane; }
    /** @brief Get the near clipping plane distance. */
    float get_near() const { return near_plane; }
    /** @brief Set the far clipping plane distance. */
    void set_far(float far_plane) { this->far_plane = far_plane; }
    /** @brief Set the near clipping plane distance. */
    void set_near(float near_plane) { this->near_plane = near_plane; }
    /** @brief Compute the orthographic projection matrix. */
    glm::mat4 get_projection_matrix() const {
        return glm::orthoLH(rect.left, rect.right, rect.bottom, rect.top, near_plane, far_plane);
    }
    /** @brief Compute the 8 corners of the view frustum. */
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return {glm::vec3(rect.left, rect.bottom, near_plane), glm::vec3(rect.right, rect.bottom, near_plane),
                glm::vec3(rect.right, rect.top, near_plane),   glm::vec3(rect.left, rect.top, near_plane),
                glm::vec3(rect.left, rect.bottom, far_plane),  glm::vec3(rect.right, rect.bottom, far_plane),
                glm::vec3(rect.right, rect.top, far_plane),    glm::vec3(rect.left, rect.top, far_plane)};
    }
};
/** @brief Perspective camera projection with field of view, aspect ratio,
 * and near/far planes. */
EPIX_EXPORT struct PerspectiveProjection {
    float fov          = glm::radians(45.0f);  // Field of view in radians
    float aspect_ratio = 1.0f;                 // Aspect ratio (width / height)
    float near_plane   = 0.1f;                 // Near clipping plane
    float far_plane    = 1000.0f;              // Far clipping plane

    /** @brief Update the aspect ratio from viewport dimensions. */
    void update(float width, float height) { aspect_ratio = width / height; }
    /** @brief Get the far clipping plane distance. */
    float get_far() const { return far_plane; }
    /** @brief Get the near clipping plane distance. */
    float get_near() const { return near_plane; }
    /** @brief Set the far clipping plane distance. */
    void set_far(float far_plane) { this->far_plane = far_plane; }
    /** @brief Set the near clipping plane distance. */
    void set_near(float near_plane) { this->near_plane = near_plane; }
    /** @brief Compute the perspective projection matrix. */
    glm::mat4 get_projection_matrix() const { return glm::perspectiveLH(fov, aspect_ratio, near_plane, far_plane); }
    /** @brief Compute the 8 corners of the perspective frustum. */
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

/** @brief Variant projection type wrapping orthographic or perspective. */
EPIX_EXPORT struct Projection {
    std::variant<OrthographicProjection, PerspectiveProjection> projection;

    Projection() : projection(OrthographicProjection{}) {}
    Projection(const OrthographicProjection& ortho) : projection(ortho) {}
    Projection(const PerspectiveProjection& perspective) : projection(perspective) {}

    /** @brief Create an orthographic projection. */
    static Projection orthographic(const OrthographicProjection& ortho = {}) { return Projection(ortho); }

    /** @brief Create a perspective projection. */
    static Projection perspective(const PerspectiveProjection& perspective = {}) { return Projection(perspective); }

    /** @brief Get the projection matrix from the active variant. */
    glm::mat4 get_projection_matrix() const {
        return std::visit([](const auto& proj) { return proj.get_projection_matrix(); }, projection);
    }
    /** @brief Get the far clipping plane distance. */
    float get_far() const {
        return std::visit([](const auto& proj) { return proj.get_far(); }, projection);
    }
    /** @brief Get the near clipping plane distance. */
    float get_near() const {
        return std::visit([](const auto& proj) { return proj.get_near(); }, projection);
    }
    /** @brief Set the far clipping plane distance. */
    void set_far(float far_plane) {
        std::visit([far_plane](auto& proj) { proj.set_far(far_plane); }, projection);
    }
    /** @brief Set the near clipping plane distance. */
    void set_near(float near_plane) {
        std::visit([near_plane](auto& proj) { proj.set_near(near_plane); }, projection);
    }
    /** @brief Compute the 8 frustum corner points. */
    std::array<glm::vec3, 8> get_frustum_corners() const {
        return std::visit([](const auto& proj) { return proj.get_frustum_corners(); }, projection);
    }
    /** @brief Update the projection for new viewport dimensions. */
    void update(float width, float height) {
        std::visit([width, height](auto& proj) { proj.update(width, height); }, projection);
    }

    /** @brief Try to get a mutable pointer to the orthographic projection. */
    std::optional<OrthographicProjection*> as_orthographic() {
        if (auto ptr = std::get_if<OrthographicProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
    /** @brief Try to get a const pointer to the orthographic projection. */
    std::optional<const OrthographicProjection*> as_orthographic() const {
        if (auto ptr = std::get_if<OrthographicProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
    /** @brief Try to get a mutable pointer to the perspective projection. */
    std::optional<PerspectiveProjection*> as_perspective() {
        if (auto ptr = std::get_if<PerspectiveProjection>(&projection)) {
            return ptr;
        } else {
            return std::nullopt;
        }
    }
    /** @brief Try to get a const pointer to the perspective projection. */
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

/** @brief System labels for camera update systems. */
EPIX_EXPORT enum class CameraUpdateSystems {
    CameraUpdateSystem = 0,
};

template <CameraProjection ProjType>
void camera_system(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Mut<Camera>, epix::ecs::Mut<ProjType>>>
        query,                                                                            // camera and projection query
    epix::ecs::Query<epix::ecs::Item<const ::epix::window::CachedWindow&>> window_query,  // window query
    epix::ecs::Query<epix::ecs::Item<const ::epix::window::CachedWindow&>,
                     epix::ecs::With<::epix::window::PrimaryWindow>> primary_window_query  // primary window query
) {
    for (auto&& [camera, proj] : query.iter()) {
        // in the body we want to update the stored target size,
        // update the projection if needed.

        std::optional<glm::uvec2> viewport_size =
            camera.get_mut().viewport.transform([](const Viewport& vp) { return glm::uvec2(vp.size); });

        glm::uvec2 target_size;
        std::visit(utils::visitor{
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

/** @brief Plugin that registers the camera update system for a specific
 * projection type.
 * @tparam ProjType Camera projection type satisfying CameraProjection. */
EPIX_EXPORT template <CameraProjection ProjType>
struct CameraProjectionPlugin {
    void attach(epix::app::App& app) {
        app.add_systems(epix::app::PostUpdate,
                        into(camera_system<ProjType>).in_set(CameraUpdateSystems::CameraUpdateSystem));
    }
};

/** @brief Label identifying the render graph assigned to a camera. */
EPIX_EXPORT struct CameraRenderGraph : public graph::GraphLabel {
    using graph::GraphLabel::GraphLabel;
};

/** @brief Per-view visibility flags of a render-world entity (Bevy
 * bevy_camera::visibility::ViewVisibility). Bit 0 is CULLED; bits 1..16 are
 * per-view visibility (up to 16 views). `get()` reports whether the entity is
 * visible to any view. Populated by the visibility systems; extraction
 * filters on it. */
EPIX_EXPORT struct ViewVisibility {
    /** @brief Raw flag storage. */
    std::uint32_t flags = 0;

    /** @brief Visible to any view (Bevy ViewVisibility::get: not culled). */
    bool get() const noexcept { return (flags & (1u << 0)) == 0; }
    /** @brief Visible in the given view (Bevy get_in_view). */
    bool get_in_view(std::uint32_t view_index) const noexcept {
        return (flags & (1u << (view_index + 1))) != 0;
    }
    /** @brief Set visibility for the given view (Bevy set_in_view). */
    void set_in_view(std::uint32_t view_index, bool visible) noexcept {
        const std::uint32_t mask = 1u << (view_index + 1);
        if (visible) {
            flags |= mask;
        } else {
            flags &= ~mask;
        }
    }
    /** @brief Mark the entity culled for all views (Bevy culled). */
    void culled() noexcept { flags |= (1u << 0); }
    /** @brief Mark the entity visible for all views (Bevy visible). */
    void visible() noexcept { flags &= ~(1u << 0); }
};

/** @brief User indication of whether an entity is visible (Bevy
 * bevy_camera::Visibility, visibility__mod.rs:39-88). Propagates down the
 * entity hierarchy; the visibility_propagate_system itself needs the transform
 * hierarchy (bevy_hierarchy ChildOf), which epix does not port yet — the marker
 * and its toggles match Bevy's interface. */
EPIX_EXPORT struct Visibility {
    /** @brief Visibility kind (Bevy Visibility variants). */
    enum class Type { Inherited, Hidden, Visible };

    /** @brief The visibility kind; default Inherited (Bevy #[default]). */
    Type type = Type::Inherited;

    static Visibility inherited() noexcept { return {Type::Inherited}; }
    static Visibility hidden() noexcept { return {Type::Hidden}; }
    static Visibility visible() noexcept { return {Type::Visible}; }

    /** @brief Toggle between Inherited and Visible (Bevy
     * Visibility::toggle_inherited_visible; Hidden unaffected). */
    void toggle_inherited_visible() noexcept {
        type = type == Type::Inherited ? Type::Visible : type == Type::Visible ? Type::Inherited : type;
    }
    /** @brief Toggle between Inherited and Hidden (Bevy
     * Visibility::toggle_inherited_hidden; Visible unaffected). */
    void toggle_inherited_hidden() noexcept {
        type = type == Type::Inherited ? Type::Hidden : type == Type::Hidden ? Type::Inherited : type;
    }
    /** @brief Toggle between Visible and Hidden (Bevy
     * Visibility::toggle_visible_hidden; Inherited unaffected). */
    void toggle_visible_hidden() noexcept {
        type = type == Type::Visible ? Type::Hidden : type == Type::Hidden ? Type::Visible : type;
    }

    bool operator==(const Visibility&) const = default;
};

/** @brief Whether or not an entity is visible in the hierarchy (Bevy
 * bevy_camera::InheritedVisibility, visibility__mod.rs:108-131). Not accurate
 * until visibility propagation runs; without the hierarchy system epix keeps it
 * as the marker Bevy's API exposes (HIDDEN/VISIBLE consts + get()). */
EPIX_EXPORT struct InheritedVisibility {
    /** @brief Raw visibility flag (Bevy's newtype field is unnamed). */
    bool is_visible = true;

    /** @brief An entity invisible in the hierarchy (Bevy
     * InheritedVisibility::HIDDEN). */
    static InheritedVisibility hidden() noexcept { return {false}; }
    /** @brief An entity visible in the hierarchy (Bevy
     * InheritedVisibility::VISIBLE). */
    static InheritedVisibility visible() noexcept { return {true}; }

    /** @brief True if the entity is visible in the hierarchy (Bevy
     * InheritedVisibility::get). */
    bool get() const noexcept { return is_visible; }

    bool operator==(const InheritedVisibility&) const = default;
};

/** @brief Usages of a camera's main textures (Bevy
 * bevy_camera::CameraMainTextureUsages). Default: RENDER_ATTACHMENT |
 * TEXTURE_BINDING (the blit samples the main texture). */
EPIX_EXPORT struct CameraMainTextureUsages {
    /** @brief Texture usages of the main textures. */
    wgpu::TextureUsage usage =
        wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding;
};

EPIX_EXPORT struct ExtractedCamera {
    // this render target is a normalized one, which means if it is a WindowRef and is primary, the entity field will
    // point to the actual primary window entity.
    RenderTarget render_target;
    glm::uvec2 viewport_size;
    glm::uvec2 target_size;
    std::optional<Viewport> viewport;
    CameraRenderGraph render_graph;
    std::ptrdiff_t order;
    std::optional<ClearColor> clear_color;
    /** @brief Whether the camera renders through an HDR intermediate texture
     * (Bevy ExtractedCamera::hdr). */
    bool hdr = false;
    /** @brief Index of this camera among cameras targeting the same render
     * target at the same order (Bevy
     * ExtractedCamera::sorted_camera_index_for_target). */
    std::optional<std::size_t> sorted_camera_index_for_target;
    /** @brief Which render layers this camera renders. Default: all layers. */
    RenderLayers render_layer = RenderLayers::all();
};
}  // namespace epix::render::camera
namespace epix::render::view {
/** @brief Forward declaration (defined below). */
EPIX_EXPORT struct ViewTargetAttachments;
/** @brief Forward declaration (defined below; used by extract_cameras and
 * prepare_view_target). */
enum class Msaa : std::uint32_t;

/**
 * @brief Stable cross-frame identifier for a render-world view (Bevy
 * RetainedViewEntity).
 */
EPIX_EXPORT struct RetainedViewEntity {
    /** @brief Main-world entity this view corresponds to. */
    sync_world::MainEntity main_entity;
    /** @brief Auxiliary entity (e.g. shadow-casting camera), or std::nullopt. */
    std::optional<sync_world::MainEntity> auxiliary_entity;
    /** @brief Subview index (0 for cameras; cascade/face index for shadow views). */
    std::uint32_t subview_index = 0;

    bool operator==(const RetainedViewEntity&) const = default;

    /** @brief Create from main entity, optional auxiliary entity, and subview
     * index (Bevy RetainedViewEntity::new, view/mod.rs:243-253). */
    static RetainedViewEntity create(sync_world::MainEntity main_entity,
                                     std::optional<sync_world::MainEntity> auxiliary_entity,
                                     std::uint32_t subview_index) {
        return RetainedViewEntity{main_entity, auxiliary_entity, subview_index};
    }
};

/** @brief Extracted view data: projection, transform, viewport and HDR
 * state for a single camera (Bevy ExtractedView).
 *
 * Field-name notes: Bevy names the projection clip_from_view and the
 * transform world_from_view; epix keeps the legacy field names projection
 * / transform for compatibility, with clip_from_view() /
 * world_from_view() accessors for the Bevy names. */
EPIX_EXPORT struct ExtractedView {
    /** @brief Stable identifier of the main-world view this render view
     * corresponds to (Bevy retained_view_entity). */
    RetainedViewEntity retained_view_entity;
    /** @brief Clip-from-view (projection) matrix (Bevy clip_from_view). */
    glm::mat4 projection;
    /** @brief World-from-view transform (Bevy world_from_view). */
    transform::GlobalTransform transform;
    /** @brief Optional pre-computed clip-from-world matrix; overrides the
     * derived value when set (Bevy clip_from_world). */
    std::optional<glm::mat4> clip_from_world;
    /** @brief Whether this view renders through an HDR intermediate texture
     * (Bevy hdr). */
    bool hdr = false;
    /** @brief Viewport as (origin.x, origin.y, width, height) (Bevy viewport). */
    glm::uvec4 viewport = glm::uvec4(0, 0, 0, 0);
    /** @brief Invert culling for mirrored views (Bevy invert_culling). */
    bool invert_culling = false;

    /** @brief Bevy name for the projection matrix. */
    const glm::mat4& clip_from_view() const noexcept { return projection; }
    /** @brief Bevy name for the world-from-view transform matrix. */
    glm::mat4 world_from_view() const noexcept { return transform.matrix; }
    /** @brief The clip-from-world matrix, either the cached one or derived. */
    glm::mat4 clip_from_world_or_derived() const noexcept {
        return clip_from_world.value_or(projection * glm::inverse(transform.matrix));
    }
    /** @brief Create a 3D rangefinder for this view (Bevy ExtractedView::rangefinder3d). */
    phase::ViewRangefinder3d rangefinder3d() const noexcept {
        return phase::ViewRangefinder3d::from_world_from_view(transform.matrix);
    }
};

/** @brief View frustum as 6 plane half-spaces (Bevy Frustum). Each plane is
 * (normal.xyz, d) with a point in front when dot(normal, p) + d >= 0. */
EPIX_EXPORT struct Frustum {
    /** @brief The six planes: left, right, bottom, top, near, far. */
    std::array<glm::vec4, 6> planes{};

    /** @brief Extract the planes from a clip-from-world matrix (Bevy
     * Frustum::from_view_projection, Gribb-Hartmann). */
    static Frustum from_view_projection(const glm::mat4& clip_from_world) noexcept {
        Frustum frustum;
        // glm mat4 is column-major; extract matrix rows first.
        auto row = [&](std::size_t i) {
            return glm::vec4{clip_from_world[0][i], clip_from_world[1][i], clip_from_world[2][i],
                             clip_from_world[3][i]};
        };
        const auto row3 = row(3);
        auto extract    = [&](std::size_t index, glm::vec4 plane) {
            const float len = glm::length(glm::vec3(plane));
            if (len > 0.0f) plane /= len;
            frustum.planes[index] = plane;
        };
        extract(0, row3 + row(0));  // left
        extract(1, row3 - row(0));  // right
        extract(2, row3 + row(1));  // bottom
        extract(3, row3 - row(1));  // top
        extract(4, row3 + row(2));  // near
        extract(5, row3 - row(2));  // far
        return frustum;
    }
};

/** @brief Component listing entities visible to a camera view, keyed by
 * visibility class (Bevy bevy_camera::VisibleEntities,
 * visibility__mod.rs:279-316). */
EPIX_EXPORT struct VisibleEntities {
    /** @brief Visible entity IDs per visibility class (Bevy
     * TypeIdMap<Vec<Entity>>). */
    std::unordered_map<meta::type_index, std::vector<epix::ecs::Entity>> entities;

    /** @brief Entities visible for the given type id; empty when absent
     * (Bevy VisibleEntities::get). */
    const std::vector<epix::ecs::Entity>& get(const meta::type_index& type_id) const {
        static const std::vector<epix::ecs::Entity> kEmpty;
        if (auto it = entities.find(type_id); it != entities.end()) return it->second;
        return kEmpty;
    }
    /** @brief Mutable access, inserting an empty list if absent (Bevy
     * VisibleEntities::get_mut). */
    std::vector<epix::ecs::Entity>& get_mut(const meta::type_index& type_id) { return entities[type_id]; }
    /** @brief Iterate the visible entities of the given type (Bevy
     * VisibleEntities::iter, DoubleEndedIterator). */
    std::span<const epix::ecs::Entity> iter(const meta::type_index& type_id) const { return get(type_id); }
    /** @brief Number of visible entities of the given type (Bevy
     * VisibleEntities::len). */
    std::size_t len(const meta::type_index& type_id) const { return get(type_id).size(); }
    /** @brief Whether any entity of the given type is visible (Bevy
     * VisibleEntities::is_empty). */
    bool is_empty(const meta::type_index& type_id) const { return get(type_id).empty(); }
    /** @brief Clear the given type's list, keeping the allocation (Bevy
     * VisibleEntities::clear). */
    void clear(const meta::type_index& type_id) { get_mut(type_id).clear(); }
    /** @brief Clear all lists, keeping allocations (Bevy
     * VisibleEntities::clear_all). */
    void clear_all() {
        for (auto& [type_id, list] : entities) {
            (void)type_id;
            list.clear();
        }
    }
};
/**
 * @brief A wrapper around a texture view used as the final output color
 * attachment of a view target (Bevy `OutputColorAttachment`).
 */

EPIX_EXPORT struct OutputColorAttachment {
    /** @brief The output texture view (e.g. the swapchain view). */
    wgpu::TextureView view;
    /** @brief Format of the output texture. */
    wgpu::TextureFormat view_format = wgpu::TextureFormat::eUndefined;
    /** @brief True until the first get_attachment call of the frame. */
    std::shared_ptr<std::atomic<bool>> is_first_call;

    OutputColorAttachment() : is_first_call(std::make_shared<std::atomic<bool>>(true)) {}

    /** @brief Create from a view + format (Bevy OutputColorAttachment::new). */
    static OutputColorAttachment create(wgpu::TextureView view, wgpu::TextureFormat view_format) {
        OutputColorAttachment attachment;
        attachment.view         = std::move(view);
        attachment.view_format  = view_format;
        attachment.is_first_call = std::make_shared<std::atomic<bool>>(true);
        return attachment;
    }
    /** @brief The attachment; clears with the given color on first call
     * (Bevy OutputColorAttachment::get_attachment). */
    wgpu::RenderPassColorAttachment get_attachment(std::optional<glm::vec4> clear_color) const {
        const bool first_call = is_first_call->exchange(false, std::memory_order_seq_cst);
        wgpu::RenderPassColorAttachment attachment;
        attachment.setView(view)
            .setDepthSlice(~0u)
            .setLoadOp(first_call && clear_color ? wgpu::LoadOp::eClear : wgpu::LoadOp::eLoad)
            .setStoreOp(wgpu::StoreOp::eStore);
        if (first_call && clear_color) {
            attachment.setClearValue(wgpu::Color(clear_color->r, clear_color->g, clear_color->b, clear_color->a));
        }
        return attachment;
    }
    /** @brief True once a render pass has written to the output (Bevy
     * OutputColorAttachment::needs_present). */
    bool needs_present() const noexcept { return !is_first_call->load(std::memory_order_seq_cst); }
    /** @brief Mark the output as written (Bevy mark_as_cleared). */
    void mark_as_cleared() const noexcept { is_first_call->store(false, std::memory_order_seq_cst); }
};

/** @brief The double-buffered main textures of a view target plus the shared
 * A/B toggle (Bevy MainTargetTextures). The attachment type is the
 * render_resource::ColorAttachment (Bevy ColorAttachment). */
EPIX_EXPORT struct MainTargetTextures {
    /** @brief Main texture A. */
    render_resource::ColorAttachment a;
    /** @brief Main texture B. */
    render_resource::ColorAttachment b;
    /** @brief Shared toggle: 0 -> a, 1 -> b (Bevy Arc<AtomicUsize>). */
    std::shared_ptr<std::atomic<std::uint32_t>> main_texture;

    MainTargetTextures() : main_texture(std::make_shared<std::atomic<std::uint32_t>>(0)) {}
};

/**
 * @brief Component holding the render target for a camera view: the
 * double-buffered main textures, their format and the final output
 * attachment (Bevy 0.18 `ViewTarget`).
 *
 * The legacy `texture_view`/`format` fields alias the CURRENT main texture
 * view and its format, so existing nodes that bind `target.texture_view`
 * render into the main texture; the camera driver presents the output.
 */
EPIX_EXPORT struct ViewTarget {
    /** @brief Double-buffered main textures (Bevy main_textures). */
    MainTargetTextures main_textures;
    /** @brief Format of the main textures (Bevy main_texture_format). */
    wgpu::TextureFormat main_texture_format = wgpu::TextureFormat::eUndefined;
    /** @brief Shared A/B toggle (Bevy main_texture: Arc<AtomicUsize>). */
    std::shared_ptr<std::atomic<std::uint32_t>> main_texture;
    /** @brief Final output attachment (Bevy out_texture). */
    OutputColorAttachment out_texture;

    /** @brief Create a view target with a fresh A/B toggle and empty output. */
    ViewTarget() : main_texture(std::make_shared<std::atomic<std::uint32_t>>(0)) {}

    /** @brief Legacy alias: texture view of the current main texture (epix
     * extension so existing render nodes keep binding `target.texture_view`). */
    wgpu::TextureView texture_view;
    /** @brief Legacy alias: format of the current main texture (epix
     * extension; equals `main_texture_format`). */
    wgpu::TextureFormat format;

    /** @brief The color attachment of the current main texture (Bevy
     * get_color_attachment). */
    wgpu::RenderPassColorAttachment get_color_attachment() const {
        return current_index() == 0 ? main_textures.a.get_attachment() : main_textures.b.get_attachment();
    }
    /** @brief The unsampled attachment of the current main texture. */
    wgpu::RenderPassColorAttachment get_unsampled_color_attachment() const {
        return current_index() == 0 ? main_textures.a.get_unsampled_attachment() : main_textures.b.get_unsampled_attachment();
    }
    /** @brief The current main texture view (Bevy main_texture_view). */
    const wgpu::TextureView& main_texture_view() const {
        return current_index() == 0 ? main_textures.a.texture.default_view : main_textures.b.texture.default_view;
    }
    /** @brief The other (non-current) main texture view (Bevy
     * main_texture_other_view). */
    const wgpu::TextureView& main_texture_other_view() const {
        return current_index() == 0 ? main_textures.b.texture.default_view : main_textures.a.texture.default_view;
    }
    /** @brief The current main texture (Bevy main_texture; renamed because
     * C++ cannot share the name with the A/B toggle field). */
    const wgpu::Texture& current_main_texture() const {
        return current_index() == 0 ? main_textures.a.texture.texture : main_textures.b.texture.texture;
    }
    /** @brief Whether the main texture is HDR (Bevy is_hdr). */
    bool is_hdr() const noexcept { return main_texture_format == wgpu::TextureFormat::eRGBA16Float; }
    /** @brief Whether the output needs to be presented (Bevy needs_present). */
    bool needs_present() const noexcept { return out_texture.needs_present(); }

    /** @brief Flip the A/B toggle, returning the source view/texture that the
     * caller must copy to the returned destination (Bevy
     * post_process_write). The destination is marked as cleared. */
    struct PostProcessWrite {
        wgpu::TextureView source;
        wgpu::Texture source_texture;
        wgpu::TextureView destination;
        wgpu::Texture destination_texture;
    };
    PostProcessWrite post_process_write() const {
        // Mutates only the shared A/B toggle and the attachments' first-call
        // flags (both atomic), so it is safe on a const view (Bevy takes
        // &self).
        const std::uint32_t old_is_a = main_texture->fetch_xor(1, std::memory_order_seq_cst);
        if (old_is_a == 0) {
            main_textures.b.mark_as_cleared();
            return PostProcessWrite{main_textures.a.texture.default_view, main_textures.a.texture.texture,
                                    main_textures.b.texture.default_view, main_textures.b.texture.texture};
        }
        main_textures.a.mark_as_cleared();
        return PostProcessWrite{main_textures.b.texture.default_view, main_textures.b.texture.texture,
                                main_textures.a.texture.default_view, main_textures.a.texture.texture};
    }

   private:
    std::uint32_t current_index() const noexcept {
        return main_texture ? main_texture->load(std::memory_order_seq_cst) : 0;
    }
};
/** @brief Component holding the depth texture and attachment for a camera
 * (Bevy ViewDepthTexture, view/mod.rs:887-904). */
EPIX_EXPORT struct ViewDepthTexture {
    /** @brief The depth texture. */
    wgpu::Texture texture;
    /** @brief The depth attachment (view + first-call clear). */
    render_resource::DepthAttachment attachment;

    /** @brief Create from a texture and view (legacy constructor; the
     * attachment gets no clear value). */
    static ViewDepthTexture create(wgpu::Texture tex, wgpu::TextureView view) {
        return ViewDepthTexture{std::move(tex), render_resource::DepthAttachment(std::move(view), std::nullopt)};
    }
};

EPIX_EXPORT struct UVec2Hash {
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
/** @brief Cache of depth textures keyed by viewport size to avoid
 * re-creation each frame. */
EPIX_EXPORT struct ViewDepthCache {
    /** @brief Map from viewport dimensions to cached depth textures. */
    std::unordered_map<glm::uvec2, wgpu::Texture, UVec2Hash> cache;
};

/** @brief Plugin that registers view extraction, target preparation, and
 * depth buffer creation systems. */
EPIX_EXPORT struct ViewPlugin {
    void attach(epix::app::App& app);
};

void prepare_view_target(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                              const camera::ExtractedCamera&,
                                              const ExtractedView&,
                                              const Msaa&>> views,
    epix::ecs::Commands cmd,
    epix::ecs::Res<window::ExtractedWindows> extracted_windows,
    epix::ecs::Res<wgpu::Device> device,
    epix::ecs::ResMut<ViewTargetAttachments> view_target_attachments);
void create_view_depth(epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const camera::ExtractedCamera&>> views,
                       epix::ecs::Res<wgpu::Device> device,
                       epix::ecs::Res<wgpu::Queue> queue,
                       epix::ecs::ResMut<ViewDepthCache> depth_cache,
                       epix::ecs::Commands cmd);

/** @brief Uniform buffer data for a view (Bevy 0.18 ViewUniform).
 *
 * Field order and sizes match the WGSL std140 layout emitted by Bevy's
 * ShaderType derive (view.wgsl): 7 mat4s, world_position + exposure,
 * viewport, main_pass_viewport, 6 frustum half-spaces, the packed
 * ColorGradingUniform, mip_bias and frame_count. sizeof == 768. */
EPIX_EXPORT struct ViewUniform {
    /** @brief Clip-from-world matrix (Bevy clip_from_world). */
    glm::mat4 clip_from_world;
    /** @brief Clip-from-world without temporal jitter (Bevy unjittered_clip_from_world). */
    glm::mat4 unjittered_clip_from_world;
    /** @brief World-from-clip matrix (Bevy world_from_clip). */
    glm::mat4 world_from_clip;
    /** @brief World-from-view matrix (Bevy world_from_view). */
    glm::mat4 world_from_view;
    /** @brief View-from-world matrix (Bevy view_from_world). */
    glm::mat4 view_from_world;
    /** @brief Clip-from-view (projection) matrix (Bevy clip_from_view). */
    glm::mat4 clip_from_view;
    /** @brief View-from-clip matrix (Bevy view_from_clip). */
    glm::mat4 view_from_clip;
    /** @brief Camera position in world space (Bevy world_position). */
    glm::vec3 world_position;
    /** @brief Exposure (Bevy exposure, Exposure::default() == 1.0). */
    float exposure = 1.0f;
    /** @brief Viewport (x_origin, y_origin, width, height). */
    glm::vec4 viewport = glm::vec4(0.0f);
    /** @brief Main-pass viewport (Bevy main_pass_viewport). */
    glm::vec4 main_pass_viewport = glm::vec4(0.0f);
    /** @brief 6 world-space half spaces: left, right, top, bottom, near, far. */
    std::array<glm::vec4, 6> frustum{};
    /** @brief Packed color grading values (Bevy color_grading). */
    ColorGradingUniform color_grading{};
    /** @brief Manual mip bias for the camera's textures (Bevy mip_bias). */
    float mip_bias = 0.0f;
    /** @brief Frame count since app start (Bevy frame_count). */
    std::uint32_t frame_count = 0;
    /** @brief Pad to WGSL std140 struct size 768. */
    float _pad_tail[2]{};
};
static_assert(sizeof(ViewUniform) == 768);
struct UniformBuffer {
    wgpu::Buffer buffer;
};
/** @brief Component holding the bind group for the view uniform buffer. */
EPIX_EXPORT struct ViewBindGroup {
    /** @brief Bind group exposing the ViewUniform to shaders. */
    wgpu::BindGroup bind_group;
};
/** @brief Resource holding the bind group layout for view uniform
 * binding. */
EPIX_EXPORT struct ViewUniformBindingLayout {
    wgpu::BindGroupLayout layout;
    ViewUniformBindingLayout(epix::ecs::World& world)
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
/** @brief Render command template that binds the view uniform buffer at
 * the specified bind group slot.
 * @tparam Slot Bind group index. */
EPIX_EXPORT template <std::size_t Slot>
struct BindViewUniform {
    template <render::phase::PhaseItem P>
    struct Command {
        void prepare(const epix::ecs::World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const P&,
            epix::ecs::Item<const ViewBindGroup&> view_bind_group,
            std::optional<epix::ecs::Item<>> entity_item,
            epix::ecs::ParamSet<>,
            const wgpu::RenderPassEncoder& encoder) {
            encoder.setBindGroup(Slot, std::get<0>(*view_bind_group).bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};
}  // namespace epix::render::view
namespace epix::render::camera {
/** @brief Manual mip bias for the camera's textures (Bevy MipBias). Defined below; forward-declared for extract_cameras. */
struct MipBias;

/** @brief System that extracts camera data into the render world. */
EPIX_EXPORT void extract_cameras(
    epix::ecs::Commands cmd,
    epix::ecs::Res<ClearColor> global_clear_color,
    epix::app::Extract<epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity,
                                                        const Camera&,
                                                        const CameraRenderGraph&,
                                                        const transform::GlobalTransform&,
                                                        const view::VisibleEntities&,
                                                        const view::Frustum&,
                                                        epix::ecs::Opt<const RenderLayers&>,
                                                        epix::ecs::Opt<const camera::MipBias&>,
                                                        const view::Msaa&>>> cameras,
    epix::app::Extract<
        epix::ecs::Query<epix::ecs::Entity, epix::ecs::With<::epix::window::PrimaryWindow, ::epix::window::Window>>>
        primary_window);

/** @brief Label for the camera driver node in the render graph. */
EPIX_EXPORT inline constexpr struct CameraDriverNodeLabelT {
} CameraDriverNodeLabel;

struct CameraPlugin {
    void attach(epix::app::App& app);
};
/** @brief Bundle for spawning a camera entity with all required
 * components (Camera, Projection, RenderGraph, Transform, VisibleEntities).
 */
EPIX_EXPORT struct CameraBundle {
    Camera camera;
    Projection projection;
    CameraRenderGraph render_graph;
    transform::Transform transform;
    view::VisibleEntities visible;
    /** @brief Which layers this camera renders. Bevy default: layer 0 only
     * (a camera without an explicit RenderLayers sees layer 0, render_layers.rs:45-52). */
    RenderLayers render_layer = RenderLayers::layer(0);

    CameraBundle(const CameraRenderGraph& graph) : render_graph(graph) {}

    static CameraBundle with_render_graph(const CameraRenderGraph& graph) {
        CameraBundle bundle(graph);
        return bundle;
    }
};
}  // namespace epix::render::camera

template <>
struct epix::ecs::Bundle<epix::render::camera::CameraBundle> {
    static void get_components(render::camera::CameraBundle& bundle,
                               utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) noexcept {
        write_component([&](void* ptr) { new (ptr) render::camera::Camera(std::move(bundle.camera)); });
        write_component([&](void* ptr) { new (ptr) render::camera::Projection(std::move(bundle.projection)); });
        write_component(
            [&](void* ptr) { new (ptr) render::camera::CameraRenderGraph(std::move(bundle.render_graph)); });
        write_component([&](void* ptr) { new (ptr) transform::Transform(std::move(bundle.transform)); });
        write_component([&](void* ptr) { new (ptr) render::view::VisibleEntities(std::move(bundle.visible)); });
        write_component([&](void* ptr) { new (ptr) render::camera::RenderLayers(std::move(bundle.render_layer)); });
    }
    static auto type_ids(const epix::ecs::Components& components) {
        return std::array<std::optional<epix::ecs::TypeId>, 6>{
            components.get_id<render::camera::Camera>(),
            components.get_id<render::camera::Projection>(),
            components.get_id<render::camera::CameraRenderGraph>(),
            components.get_id<transform::Transform>(),
            components.get_id<render::view::VisibleEntities>(),
            components.get_id<render::camera::RenderLayers>(),
        };
    }
    static auto register_components(epix::ecs::ComponentsRegistrator& components) {
        std::vector<epix::ecs::TypeId> ids;
        ids.push_back(components.register_component<render::camera::Camera>());
        ids.push_back(components.register_component<render::camera::Projection>());
        ids.push_back(components.register_component<render::camera::CameraRenderGraph>());
        ids.push_back(components.register_component<transform::Transform>());
        ids.push_back(components.register_component<render::view::VisibleEntities>());
        ids.push_back(components.register_component<render::camera::RenderLayers>());
        return ids;
    }
};
static_assert(epix::ecs::is_bundle<epix::render::camera::CameraBundle>);

namespace epix::render::view {
/** @brief MSAA sample count for a camera view (Bevy 0.18 Msaa). */
EPIX_EXPORT enum class Msaa : std::uint32_t {
    Off = 1,
    Sample2 = 2,
    Sample4 = 4,
    Sample8 = 8,
};

/** @brief Sample count of an Msaa value. */
EPIX_EXPORT inline std::uint32_t samples(Msaa msaa) noexcept { return static_cast<std::uint32_t>(msaa); }

/** @brief Convert a raw sample count to Msaa. Throws for unsupported counts. */
EPIX_EXPORT inline Msaa msaa_from_samples(std::uint32_t sample_count) {
    switch (sample_count) {
        case 1: return Msaa::Off;
        case 2: return Msaa::Sample2;
        case 4: return Msaa::Sample4;
        case 8: return Msaa::Sample8;
        default: throw std::runtime_error("Unsupported MSAA sample count: " + std::to_string(sample_count));
    }
}

/** @brief Marker component: render through an intermediate HDR texture (Bevy Hdr). */
EPIX_EXPORT struct Hdr {};

/** @brief Marker component: the view does not support indirect drawing (Bevy NoIndirectDrawing). */
EPIX_EXPORT struct NoIndirectDrawing {};

/** @brief Marker component: the view does not support CPU culling (Bevy NoCpuCulling). */
EPIX_EXPORT struct NoCpuCulling {};

/**
 * @brief Resource holding the dynamic uniform buffer for all view uniforms and
 * the per-view offsets (Bevy ViewUniforms).
 */
EPIX_EXPORT struct ViewUniforms {
    /** @brief Dynamic uniform buffer containing all ViewUniforms. */
    render_resource::DynamicUniformBuffer<ViewUniform> uniforms;
    /** @brief Per-view dynamic offsets into uniforms. */
    std::vector<std::uint32_t> offsets;
};

/** @brief Component storing the offset of a view's uniform inside ViewUniforms (Bevy ViewUniformOffset). */
EPIX_EXPORT struct ViewUniformOffset {
    /** @brief Dynamic buffer offset in bytes. */
    std::uint32_t offset = 0;
};


/**
 * @brief Render-world counterpart of VisibleEntities (Bevy RenderVisibleEntities).
 */
EPIX_EXPORT struct RenderVisibleEntities {
    /** @brief Visible (render-entity, main-entity) pairs per visibility class. */
    std::unordered_map<meta::type_index, std::vector<std::pair<epix::ecs::Entity, sync_world::MainEntity>>> entities;

    /** @brief Entities visible to the view for the given query-filter type
     * (Bevy RenderVisibleEntities::get<QF>, empty slice when absent). */
    template <typename QF>
    const std::vector<std::pair<epix::ecs::Entity, sync_world::MainEntity>>& get() const {
        static const std::vector<std::pair<epix::ecs::Entity, sync_world::MainEntity>> kEmpty;
        if (auto it = entities.find(meta::type_index(meta::type_id<QF>())); it != entities.end()) return it->second;
        return kEmpty;
    }
    /** @brief Span over the visible entities for the given type (Bevy
     * RenderVisibleEntities::iter<QF>, DoubleEndedIterator). */
    template <typename QF>
    std::span<const std::pair<epix::ecs::Entity, sync_world::MainEntity>> iter() const {
        return get<QF>();
    }
    /** @brief Number of visible entities for the given type (Bevy
     * RenderVisibleEntities::len<QF>). */
    template <typename QF>
    std::size_t len() const {
        return get<QF>().size();
    }
    /** @brief Whether any entity of the given type is visible (Bevy
     * RenderVisibleEntities::is_empty<QF>). */
    template <typename QF>
    bool is_empty() const {
        return get<QF>().empty();
    }
};

/**
 * @brief Per-view render targets keyed by normalized render target (Bevy ViewTargetAttachments).
 */
EPIX_EXPORT struct ViewTargetAttachments {
    /** @brief One shared output attachment per render target, so the output is
     * cleared at most once per frame and later cameras composite over it
     * (Bevy ViewTargetAttachments). */
    std::unordered_map<camera::RenderTargetId, OutputColorAttachment, camera::RenderTargetIdHash> attachments;
};

/** @brief Clears the per-frame view target attachments (Bevy
 * clear_view_attachments, view/mod.rs:1042-1044). Registered in
 * RenderSystems::ManageViews before create_surfaces. */
void clear_view_attachments(epix::ecs::ResMut<ViewTargetAttachments> view_target_attachments);
/** @brief Removes the ViewTarget of cameras targeting a window that was
 * resized or changed present mode, so prepare_view_target recreates them at
 * the new size (Bevy cleanup_view_targets_for_resize, view/mod.rs:1046-1059).
 * Registered in RenderSystems::ManageViews before create_surfaces. */
void cleanup_view_targets_for_resize(
    epix::ecs::Commands cmd,
    epix::ecs::Res<window::ExtractedWindows> windows,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const camera::ExtractedCamera&>> cameras);

}  // namespace epix::render::view

namespace epix::render::camera {
/** @brief Error returned when render target information cannot be resolved (Bevy MissingRenderTargetInfoError). */
EPIX_EXPORT struct MissingRenderTargetInfoError {
    /** @brief Description of the unresolvable target. */
    std::string message;
    std::string to_string() const { return "missing render target info: " + message; }
};

/** @brief Per-camera sort entry (Bevy SortedCamera). */
EPIX_EXPORT struct SortedCamera {
    /** @brief Render-world camera entity. */
    epix::ecs::Entity entity;
    /** @brief Camera order; lower renders first (behind). */
    std::ptrdiff_t order = 0;
    /** @brief The normalized render target this camera renders to (Bevy SortedCamera::target). */
    std::optional<RenderTarget> target;
    /** @brief Whether this camera uses an HDR intermediate texture. */
    bool hdr = false;

    /** @brief Comparable key used to group same-order cameras by target type
     * and identity (Bevy sorts by (order, target)). */
    std::pair<std::ptrdiff_t, std::size_t> sort_key() const noexcept {
        std::size_t target_key = 0;
        if (target) {
            target_key = std::visit(
                utils::visitor{
                    [](const wgpu::Texture&) -> std::size_t { return 1; },
                    [](const WindowRef& w) -> std::size_t {
                        return 2 + static_cast<std::size_t>(w.window_entity.index);
                    },
                },
                *target);
        }
        return {order, target_key};
    }
};

/** @brief Resource holding cameras sorted by order (Bevy SortedCameras). */
EPIX_EXPORT struct SortedCameras {
    /** @brief Sorted camera list. */
    std::vector<SortedCamera> cameras;
};

/**
 * @brief System that sorts all extracted cameras by order, packing cameras
 * targeting the same render target together (Bevy sort_cameras). Also
 * assigns each camera its per-target index.
 */
EPIX_EXPORT inline void sort_cameras(epix::ecs::ResMut<SortedCameras> sorted_cameras,
                                     epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, epix::ecs::Mut<ExtractedCamera>>> cameras) {
    sorted_cameras->cameras.clear();
    for (auto&& [entity, camera] : cameras.iter()) {
        sorted_cameras->cameras.push_back(
            SortedCamera{entity, camera.get().order, camera.get().render_target, camera.get().hdr});
    }
    // Bevy uses a stable sort (sort_by): cameras with equal (order, target)
    // keep their extraction order.
    std::ranges::stable_sort(sorted_cameras->cameras,
                             [](const SortedCamera& a, const SortedCamera& b) { return a.sort_key() < b.sort_key(); });
    // Assign per-target indices in sorted order, keyed by (target, hdr)
    // (Bevy camera.rs:744-763). Textures have a distinct identity per handle,
    // so cameras targeting different textures never share a counter.
    std::unordered_map<std::uint64_t, std::size_t> counts;
    std::unordered_map<epix::ecs::Entity, std::size_t> index_for_entity;
    for (const auto& cam : sorted_cameras->cameras) {
        if (!cam.target) continue;
        const std::uint64_t key = (cam.target->identity().value << 1) | static_cast<std::uint64_t>(cam.hdr);
        index_for_entity[cam.entity] = counts[key]++;
    }
    for (auto&& [entity, camera] : cameras.iter()) {
        if (auto it = index_for_entity.find(entity); it != index_for_entity.end()) {
            camera.get_mut().sorted_camera_index_for_target = it->second;
        }
    }
}

/** @brief Per-frame temporal jitter in texels (Bevy TemporalJitter). */
EPIX_EXPORT struct TemporalJitter {
    /** @brief Jitter offset in texels. */
    glm::vec2 offset = glm::vec2(0.0f);
};

/** @brief Manual mip bias for the camera's textures (Bevy MipBias). */
EPIX_EXPORT struct MipBias {
    /** @brief Bias applied to mip selection (Bevy default -1.0, camera.rs:698-702). */
    float bias = -1.0f;
};

}  // namespace epix::render::camera

/** @brief Hash for `RetainedViewEntity` (Bevy derives Hash). */
template <>
struct std::hash<::epix::render::view::RetainedViewEntity> {
    std::size_t operator()(const ::epix::render::view::RetainedViewEntity& r) const noexcept {
        std::size_t h = std::hash<::epix::render::sync_world::MainEntity>{}(r.main_entity);
        h ^= r.auxiliary_entity.transform([](const ::epix::render::sync_world::MainEntity& aux) {
                 return std::hash<::epix::render::sync_world::MainEntity>{}(aux);
             }).value_or(0u) +
             0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(r.subview_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

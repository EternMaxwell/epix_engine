#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/transform.hpp>
#include <epix/utils.hpp>
#include <epix/window.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/camera/clear_color.hpp>
#include <epix/camera/projection.hpp>
#include <epix/camera/visibility.hpp>

namespace epix::camera {

/** @brief Defines a sub-region of the render target for camera output (Bevy
 * bevy_camera::Viewport). */
EPIX_EXPORT struct Viewport {
    /** @brief Top-left position of the viewport in pixels. */
    glm::uvec2 pos{0, 0};
    /** @brief Size of the viewport in pixels. */
    glm::uvec2 size{1, 1};
    /** @brief Depth range for the viewport (min, max). */
    std::pair<float, float> depth_range{0.0f, 1.0f};

    /** @brief Clamp this viewport to a physical target extent (Bevy
     * Viewport::clamp_to_size). */
    void clamp_to_size(glm::uvec2 target_size) noexcept {
        auto clamp_axis = [](std::uint32_t& origin, std::uint32_t& extent, std::uint32_t target) {
            // Avoid overflow in origin + extent while preserving Bevy's
            // "just inside" behavior for an origin beyond the target.
            if (origin <= target && extent <= target - origin) return;
            if (origin < target) {
                extent = target - origin;
            } else if (target > 0) {
                origin = target - 1;
                extent = 1;
            } else {
                origin = 0;
                extent = 0;
            }
        };
        clamp_axis(pos.x, size.x, target_size.x);
        clamp_axis(pos.y, size.y, target_size.y);
    }

    /** @brief Returns a viewport derived from an optional camera viewport and
     * optional main-pass resolution override (Bevy
     * Viewport::from_viewport_and_override). */
    static std::optional<Viewport> from_viewport_and_override(
        const std::optional<Viewport>& viewport, const std::optional<glm::uvec2>& main_pass_resolution_override) {
        if (!main_pass_resolution_override) return viewport;
        Viewport result = viewport.value_or(Viewport{});
        result.size     = *main_pass_resolution_override;
        return result;
    }
};

/** @brief Reference to a window entity used as a render target.
 *
 * When primary is true, the primary window is used regardless of
 * window_entity. */
EPIX_EXPORT struct WindowRef {
    /** @brief Whether to target the primary window. */
    bool primary = true;
    /** @brief Window entity to target when primary is false. */
    epix::ecs::Entity window_entity;
};

/** @brief Stable identity of a normalized render target, used to share one
 * output attachment per target and to group cameras by target (Bevy
 * NormalizedRenderTarget as a HashMap key). */
EPIX_EXPORT struct RenderTargetId {
    std::uint64_t value = 0;
    bool operator==(const RenderTargetId&) const noexcept = default;
};

/** @brief Resolved dimensions and logical scaling of a render target (Bevy
 * RenderTargetInfo).  Window backends that do not expose DPI scaling use the
 * natural scale of 1.0. */
EPIX_EXPORT struct RenderTargetInfo {
    glm::uvec2 physical_size{0, 0};
    float scale_factor = 1.0f;
};

/** @brief Errors reported by camera world/viewport coordinate conversion
 * helpers (Bevy ViewportConversionError). */
EPIX_EXPORT enum class ViewportConversionError {
    NoViewportSize,
    PastNearPlane,
    PastFarPlane,
    InvalidData,
};

/** @brief A world-space ray emitted through a viewport point (Bevy Ray3d).
 * `direction` is always normalized when construction succeeds. */
EPIX_EXPORT struct Ray3d {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
};
/** @brief Hash for RenderTargetId. */
EPIX_EXPORT struct RenderTargetIdHash {
    std::size_t operator()(const RenderTargetId& id) const noexcept { return std::hash<std::uint64_t>{}(id.value); }
};

/** @brief A render target that is either a GPU texture or a window
 * reference (Bevy bevy_camera::RenderTarget). */
EPIX_EXPORT struct RenderTarget : std::variant<wgpu::Texture, WindowRef> {
    using std::variant<wgpu::Texture, WindowRef>::variant;
    static RenderTarget from_texture(wgpu::Texture texture) { return RenderTarget(std::move(texture)); }
    static RenderTarget from_primary() noexcept { return RenderTarget(WindowRef{true}); }
    static RenderTarget from_window(epix::ecs::Entity window_entity) noexcept {
        return RenderTarget(WindowRef{false, window_entity});
    }
    std::optional<RenderTarget> normalize(std::optional<epix::ecs::Entity> primary) const;
    /** @brief Stable identity for grouping/sorting by target. Textures are
     * keyed by their raw handle, windows by their entity uid. */
    RenderTargetId identity() const noexcept;
};

struct ComputedCameraValues {
    // A camera can be extracted before the first camera-update pass has
    // resolved its window target.  Bevy represents that state as an absent
    // physical target size; use an explicit 0x0 sentinel rather than GLM's
    // intentionally uninitialised default constructor.
    glm::mat4 projection{1.0f};
    glm::uvec2 target_size{0, 0};
    std::optional<RenderTargetInfo> target_info;
    std::optional<glm::uvec2> old_viewport_size;
    std::optional<SubCameraView> old_sub_camera_view;
};

/** @brief The final-output policy for a camera (Bevy CameraOutputMode). */
EPIX_EXPORT struct CameraOutputMode {
    enum class Type { Write, Skip } type = Type::Write;
    /** @brief Optional blend state for writing the intermediate target. */
    std::optional<wgpu::BlendState> blend_state;
    /** @brief Clear operation for the final target in Write mode. */
    ClearColorConfig clear_color = ClearColorConfig::def();

    static CameraOutputMode write(std::optional<wgpu::BlendState> blend_state = std::nullopt,
                                  ClearColorConfig clear_color = ClearColorConfig::def()) {
        return CameraOutputMode{Type::Write, std::move(blend_state), clear_color};
    }
    static CameraOutputMode skip() noexcept { return CameraOutputMode{Type::Skip}; }
};

/** @brief Extra usages requested for a camera's intermediate main textures
 * (Bevy CameraMainTextureUsages). */
EPIX_EXPORT struct CameraMainTextureUsages {
    wgpu::TextureUsage usage = wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding |
                               wgpu::TextureUsage::eCopySrc;

    /** @brief Adds usages while preserving the Bevy-compatible defaults. */
    CameraMainTextureUsages& with(wgpu::TextureUsage extra) noexcept {
        usage = usage | extra;
        return *this;
    }
};

/** @brief Camera component that controls viewport, render target, ordering,
 * and clear colour (Bevy bevy_camera::Camera).
 *
 * Cameras with higher order render on top of those with lower order.
 * The computed projection and target size are updated automatically by
 * camera systems. */
EPIX_EXPORT struct Camera {
    /** @brief The camera's viewport within the render target. */
    std::optional<Viewport> viewport;
    /** @brief Cameras with higher order are rendered on top of cameras with
     * lower order. */
    std::ptrdiff_t order = 0;
    /** @brief Whether this camera is active and should be used for rendering.
     * `is_active` is the Bevy 0.18 spelling; `active` is retained as a source
     * compatible Epix alias. */
    union {
        bool is_active = true;
        bool active;
    };
    /** @brief Legacy Epix HDR flag. Bevy 0.18 uses the standalone render
     * world `Hdr` marker; extraction prefers that marker while honouring this
     * flag for existing Epix scenes. */
    bool hdr = false;
    /** @brief Reverse face culling for mirrored views (Bevy
     * ExtractedView::invert_culling input). */
    bool invert_culling = false;
    /** @brief Final output policy (Bevy Camera::output_mode). */
    CameraOutputMode output_mode{};
    /** @brief MSAA writeback policy (Bevy Camera::msaa_writeback). */
    MsaaWriteback msaa_writeback = MsaaWriteback::Auto;
    /** @brief Optional slice of a larger, shared camera view. */
    std::optional<SubCameraView> sub_camera_view;

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

    /** @brief Converts a physical pixel size into this target's logical size
     * (Bevy Camera::to_logical). */
    std::optional<glm::vec2> to_logical(glm::uvec2 physical_size) const noexcept {
        if (!computed.target_info || computed.target_info->scale_factor <= 0.0f) return std::nullopt;
        return glm::vec2(physical_size) / computed.target_info->scale_factor;
    }
    /** @brief Physical viewport rectangle as (min, max), or no value before
     * the camera target has been resolved (Bevy physical_viewport_rect). */
    std::optional<std::pair<glm::uvec2, glm::uvec2>> physical_viewport_rect() const noexcept {
        auto size = physical_viewport_size();
        if (!size) return std::nullopt;
        const auto min = get_viewport_origin();
        return std::pair{min, min + *size};
    }
    /** @brief Logical viewport rectangle as (min, max) (Bevy
     * logical_viewport_rect). */
    std::optional<std::pair<glm::vec2, glm::vec2>> logical_viewport_rect() const noexcept {
        auto rect = physical_viewport_rect();
        if (!rect) return std::nullopt;
        auto min = to_logical(rect->first);
        auto max = to_logical(rect->second);
        if (!min || !max) return std::nullopt;
        return std::pair{*min, *max};
    }
    /** @brief The physical viewport size, if the target has been resolved
     * (Bevy physical_viewport_size). */
    std::optional<glm::uvec2> physical_viewport_size() const noexcept {
        if (viewport) return viewport->size;
        return physical_target_size();
    }
    /** @brief The logical viewport size (Bevy logical_viewport_size). */
    std::optional<glm::vec2> logical_viewport_size() const noexcept {
        auto size = physical_viewport_size();
        return size ? to_logical(*size) : std::nullopt;
    }
    /** @brief Full physical target size, ignoring the viewport (Bevy
     * physical_target_size). */
    std::optional<glm::uvec2> physical_target_size() const noexcept {
        return computed.target_info.transform([](const RenderTargetInfo& info) { return info.physical_size; });
    }
    /** @brief Full logical target size, ignoring the viewport (Bevy
     * logical_target_size). */
    std::optional<glm::vec2> logical_target_size() const noexcept {
        auto size = physical_target_size();
        return size ? to_logical(*size) : std::nullopt;
    }
    /** @brief Target DPI/logical scale factor (Bevy target_scaling_factor). */
    std::optional<float> target_scaling_factor() const noexcept {
        return computed.target_info.transform([](const RenderTargetInfo& info) { return info.scale_factor; });
    }
    /** @brief Computed clip-from-view matrix (Bevy clip_from_view). */
    const glm::mat4& clip_from_view() const noexcept { return computed.projection; }

    /** @brief Converts a world-space point to normalized device coordinates
     * (Bevy world_to_ndc). */
    std::optional<glm::vec3> world_to_ndc(const transform::GlobalTransform& camera_transform,
                                          glm::vec3 world_point) const noexcept {
        const glm::vec4 view_point = glm::inverse(camera_transform.matrix) * glm::vec4(world_point, 1.0f);
        const glm::vec4 clip_point = computed.projection * view_point;
        if (clip_point.w == 0.0f) return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip_point) / clip_point.w;
        return std::isfinite(ndc.x) && std::isfinite(ndc.y) && std::isfinite(ndc.z)
                   ? std::optional<glm::vec3>(ndc)
                   : std::nullopt;
    }
    /** @brief Converts normalized device coordinates to world space (Bevy
     * ndc_to_world). */
    std::optional<glm::vec3> ndc_to_world(const transform::GlobalTransform& camera_transform,
                                          glm::vec3 ndc_point) const noexcept {
        const glm::vec4 view_point = glm::inverse(computed.projection) * glm::vec4(ndc_point, 1.0f);
        if (view_point.w == 0.0f) return std::nullopt;
        const glm::vec4 world_point = camera_transform.matrix * (view_point / view_point.w);
        const glm::vec3 result = glm::vec3(world_point);
        return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z)
                   ? std::optional<glm::vec3>(result)
                   : std::nullopt;
    }
    /** @brief Converts a viewport pixel position to NDC (Bevy
     * viewport_to_ndc). */
    std::expected<glm::vec2, ViewportConversionError> viewport_to_ndc(glm::vec2 viewport_position) const noexcept {
        auto rect = logical_viewport_rect();
        if (!rect) return std::unexpected(ViewportConversionError::NoViewportSize);
        const glm::vec2 extent = rect->second - rect->first;
        if (extent.x <= 0.0f || extent.y <= 0.0f) return std::unexpected(ViewportConversionError::NoViewportSize);
        glm::vec2 ndc = ((viewport_position - rect->first) / extent) * 2.0f - glm::vec2(1.0f);
        ndc.y = -ndc.y;
        return ndc;
    }
    /** @brief Converts a world-space point to viewport pixels (Bevy
     * world_to_viewport). */
    std::expected<glm::vec2, ViewportConversionError> world_to_viewport(
        const transform::GlobalTransform& camera_transform, glm::vec3 world_position) const noexcept {
        auto rect = logical_viewport_rect();
        if (!rect) return std::unexpected(ViewportConversionError::NoViewportSize);
        auto ndc = world_to_ndc(camera_transform, world_position);
        if (!ndc) return std::unexpected(ViewportConversionError::InvalidData);
        if (ndc->z < 0.0f) return std::unexpected(ViewportConversionError::PastFarPlane);
        if (ndc->z > 1.0f) return std::unexpected(ViewportConversionError::PastNearPlane);
        const glm::vec2 extent = rect->second - rect->first;
        return ((glm::vec2(ndc->x, -ndc->y) + glm::vec2(1.0f)) * 0.5f) * extent + rect->first;
    }
    /** @brief Converts a world-space point to viewport pixels plus positive
     * view depth (Bevy world_to_viewport_with_depth). */
    std::expected<glm::vec3, ViewportConversionError> world_to_viewport_with_depth(
        const transform::GlobalTransform& camera_transform, glm::vec3 world_position) const noexcept {
        auto viewport = world_to_viewport(camera_transform, world_position);
        if (!viewport) return std::unexpected(viewport.error());
        auto ndc = world_to_ndc(camera_transform, world_position);
        if (!ndc) return std::unexpected(ViewportConversionError::InvalidData);
        return glm::vec3(*viewport, -depth_ndc_to_view_z(ndc->z));
    }
    /** @brief Returns the world ray passing through a viewport point (Bevy
     * viewport_to_world). */
    std::expected<Ray3d, ViewportConversionError> viewport_to_world(
        const transform::GlobalTransform& camera_transform, glm::vec2 viewport_position) const noexcept {
        auto ndc_xy = viewport_to_ndc(viewport_position);
        if (!ndc_xy) return std::unexpected(ndc_xy.error());
        auto near_point = ndc_to_world(camera_transform, glm::vec3(*ndc_xy, 1.0f));
        auto far_point = ndc_to_world(camera_transform, glm::vec3(*ndc_xy, std::numeric_limits<float>::epsilon()));
        if (!near_point || !far_point) return std::unexpected(ViewportConversionError::InvalidData);
        glm::vec3 direction = *far_point - *near_point;
        const float length = glm::length(direction);
        if (!std::isfinite(length) || length == 0.0f) return std::unexpected(ViewportConversionError::InvalidData);
        return Ray3d{*near_point, direction / length};
    }
    /** @brief Returns the XY world position at a viewport point (Bevy
     * viewport_to_world_2d). */
    std::expected<glm::vec2, ViewportConversionError> viewport_to_world_2d(
        const transform::GlobalTransform& camera_transform, glm::vec2 viewport_position) const noexcept {
        auto ndc_xy = viewport_to_ndc(viewport_position);
        if (!ndc_xy) return std::unexpected(ndc_xy.error());
        auto world = ndc_to_world(camera_transform, glm::vec3(*ndc_xy, 1.0f));
        if (!world) return std::unexpected(ViewportConversionError::InvalidData);
        return glm::vec2(*world);
    }
    /** @brief Converts NDC depth to perspective view Z (Bevy
     * depth_ndc_to_view_z). */
    float depth_ndc_to_view_z(float ndc_depth) const noexcept { return -computed.projection[3][2] / ndc_depth; }
    /** @brief Converts NDC depth to orthographic view Z (Bevy
     * depth_ndc_to_view_z_2d). */
    float depth_ndc_to_view_z_2d(float ndc_depth) const noexcept {
        return -(computed.projection[3][2] - ndc_depth) / computed.projection[2][2];
    }
};

/** @brief Texture usages requested by a 3D camera depth target (Bevy
 * `Camera3dDepthTextureUsage`). */
EPIX_EXPORT struct Camera3dDepthTextureUsage {
    std::uint32_t bits = static_cast<std::uint32_t>(wgpu::TextureUsage::eRenderAttachment);

    Camera3dDepthTextureUsage() = default;
    explicit Camera3dDepthTextureUsage(wgpu::TextureUsage usage) : bits(static_cast<std::uint32_t>(usage)) {}
    wgpu::TextureUsage usage() const noexcept { return static_cast<wgpu::TextureUsage>(bits); }
};

/** @brief Depth attachment load policy for the 3D main pass (Bevy
 * `Camera3dDepthLoadOp`). `Clear(0)` is the reverse-Z default. */
EPIX_EXPORT struct Camera3dDepthLoadOp {
    enum class Type { Clear, Load } type = Type::Clear;
    float clear_value = 0.0f;

    static constexpr Camera3dDepthLoadOp clear(float value = 0.0f) noexcept { return {Type::Clear, value}; }
    static constexpr Camera3dDepthLoadOp load() noexcept { return {Type::Load, 0.0f}; }
};

/** @brief Quality of screen-space specular transmission filtering (Bevy
 * `ScreenSpaceTransmissionQuality`). */
EPIX_EXPORT enum class ScreenSpaceTransmissionQuality { Low, Medium, High, Ultra };

/** @brief 3D camera pass configuration (Bevy `Camera3d`). The Core3D graph
 * remains an Epix graph/plugin concern; this component supplies the same
 * camera-side defaults and settings. */
EPIX_EXPORT struct Camera3d {
    Camera3dDepthLoadOp depth_load_op{};
    Camera3dDepthTextureUsage depth_texture_usages{};
    std::size_t screen_space_specular_transmission_steps = 1;
    ScreenSpaceTransmissionQuality screen_space_specular_transmission_quality = ScreenSpaceTransmissionQuality::Medium;

    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        // EPIX required components are not transitive, so mirror Camera's
        // Bevy requirements here as well as Camera + Projection.
        registrator.template register_required<Camera>([] { return Camera{}; });
        registrator.template register_required<Projection>([] { return Projection{}; });
        registrator.template register_required<transform::Transform>([] { return transform::Transform{}; });
        registrator.template register_required<VisibleEntities>([] { return VisibleEntities{}; });
        registrator.template register_required<RenderLayers>([] { return RenderLayers::layer(0); });
        registrator.template register_required<Msaa>([] { return Msaa::Sample4; });
        registrator.template register_required<CameraMainTextureUsages>([] { return CameraMainTextureUsages{}; });
        registrator.template register_required<Frustum>([] { return Frustum{}; });
    }
};

struct PhysicalCameraParameters;

/** @brief Physical camera exposure in EV100 (Bevy Exposure). */
EPIX_EXPORT struct Exposure {
    static constexpr float EV100_SUNLIGHT = 15.0f;
    static constexpr float EV100_OVERCAST = 12.0f;
    static constexpr float EV100_INDOOR   = 7.0f;
    static constexpr float EV100_BLENDER  = 9.7f;

    static constexpr Exposure sunlight() noexcept { return Exposure{EV100_SUNLIGHT}; }
    static constexpr Exposure overcast() noexcept { return Exposure{EV100_OVERCAST}; }
    static constexpr Exposure indoor() noexcept { return Exposure{EV100_INDOOR}; }
    static constexpr Exposure blender() noexcept { return Exposure{EV100_BLENDER}; }
    static Exposure from_physical_camera(const PhysicalCameraParameters& parameters) noexcept;

    float ev100 = 9.7f;  ///< Blender-compatible Bevy default.
    float exposure() const noexcept { return std::exp2(-ev100) / 1.2f; }
};

/** @brief Physical camera settings used to derive EV100 (Bevy
 * PhysicalCameraParameters). */
EPIX_EXPORT struct PhysicalCameraParameters {
    float aperture_f_stops = 1.0f;
    float shutter_speed_s  = 1.0f / 125.0f;
    float sensitivity_iso  = 100.0f;
    float sensor_height    = 0.01866f;

    float ev100() const noexcept {
        return std::log2(aperture_f_stops * aperture_f_stops * 100.0f / (shutter_speed_s * sensitivity_iso));
    }
};

inline Exposure exposure_from_physical_camera(const PhysicalCameraParameters& parameters) noexcept {
    return Exposure{parameters.ev100()};
}

inline Exposure Exposure::from_physical_camera(const PhysicalCameraParameters& parameters) noexcept {
    return exposure_from_physical_camera(parameters);
}

/** @brief Overrides the physical resolution of a camera's main pass without
 * changing its output viewport (Bevy MainPassResolutionOverride). */
EPIX_EXPORT struct MainPassResolutionOverride {
    glm::uvec2 size{1, 1};
};

// --- Camera Systems --- //

/** @brief System labels for camera update systems (Bevy CameraUpdateSystems). */
EPIX_EXPORT enum class CameraUpdateSystems {
    CameraUpdateSystem = 0,
};

template <CameraProjection ProjType>
void camera_system(
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Mut<Camera>, epix::ecs::Mut<ProjType>>>
        query,                                                                            // camera and projection query
    epix::ecs::Query<epix::ecs::Item<const ::epix::window::Window&>> window_query,  // window query
    epix::ecs::Query<epix::ecs::Item<const ::epix::window::Window&>,
                     epix::ecs::With<::epix::window::PrimaryWindow>> primary_window_query  // primary window query
) {
    for (auto&& [camera, proj] : query.iter()) {
        // update the stored target size, update the projection if needed.

        std::optional<glm::uvec2> viewport_size =
            camera.get_mut().viewport.transform([](const Viewport& vp) { return glm::uvec2(vp.size); });

        // A camera can be updated before the native backend has supplied a
        // target.  Keep that state explicitly invalid instead of permitting
        // GLM's uninitialised default constructor to reach GPU allocation.
        glm::uvec2 target_size{0, 0};
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

        auto& camera_mut = camera.get_mut();
        // Bevy clamps custom viewports after resolving the target. This also
        // handles a resize that leaves the previous viewport out of bounds.
        if (camera_mut.viewport) camera_mut.viewport->clamp_to_size(target_size);
        viewport_size = camera_mut.viewport.transform([](const Viewport& vp) { return vp.size; });
        const glm::uvec2 new_size = viewport_size.value_or(target_size);
        const bool size_changed = camera_mut.computed.target_size != target_size ||
                                  camera_mut.computed.old_viewport_size != viewport_size ||
                                  camera_mut.computed.old_sub_camera_view != camera_mut.sub_camera_view;
        camera_mut.computed.target_size       = target_size;
        camera_mut.computed.target_info       = target_size.x != 0 && target_size.y != 0
                                                    ? std::optional<RenderTargetInfo>(RenderTargetInfo{target_size, 1.0f})
                                                    : std::nullopt;
        camera_mut.computed.old_viewport_size = viewport_size;
        camera_mut.computed.old_sub_camera_view = camera_mut.sub_camera_view;

        // Bevy deliberately leaves the previous projection intact for an
        // invalid 0-sized target/viewport; updating perspective aspect ratio
        // here would create NaNs.
        // Projection parameters (for example OrthographicProjection::scale
        // changed by mouse-wheel zoom) can change without a target resize.
        // Rebuild the projection for every valid camera update so the cached
        // matrix never lags the component. This is the observable behavior of
        // Bevy's changed-camera / changed-projection update path.
        if (new_size.x != 0 && new_size.y != 0) {
            proj.get_mut().update(static_cast<float>(new_size.x), static_cast<float>(new_size.y));
            camera_mut.computed.projection = camera_mut.sub_camera_view
                                                 ? proj.get().get_projection_matrix_for_sub(*camera_mut.sub_camera_view)
                                                 : proj.get().get_projection_matrix();
        }
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

/** @brief Plugin that registers camera_system (projection updates), the
 * visibility systems, the projection plugins, and the ClearColor resource
 * (Bevy bevy_camera::CameraPlugin — the user-facing, main-world camera
 * plugin; render-world extraction lives in the render module). */
EPIX_EXPORT struct CameraPlugin {
    void attach(epix::app::App& app);
};

}  // namespace epix::camera

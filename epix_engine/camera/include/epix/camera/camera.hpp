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
#include <expected>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <stdexcept>
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
    glm::uvec2 physical_position{0, 0};
    /** @brief Size of the viewport in pixels. */
    glm::uvec2 physical_size{1, 1};
    /** @brief Depth range for the viewport (min, max). */
    std::pair<float, float> depth{0.0f, 1.0f};

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
        clamp_axis(physical_position.x, physical_size.x, target_size.x);
        clamp_axis(physical_position.y, physical_size.y, target_size.y);
    }

    /** @brief Returns a viewport derived from an optional camera viewport and
     * optional main-pass resolution override (Bevy
     * Viewport::from_viewport_and_override). */
    static std::optional<Viewport> from_viewport_and_override(
        const std::optional<Viewport>& viewport, const std::optional<glm::uvec2>& main_pass_resolution_override) {
        if (!main_pass_resolution_override) return viewport;
        Viewport result      = viewport.value_or(Viewport{});
        result.physical_size = *main_pass_resolution_override;
        return result;
    }
};

EPIX_EXPORT struct ManualTextureViewHandle {
    std::uint32_t id                                               = 0;
    bool operator==(const ManualTextureViewHandle&) const noexcept = default;
};

EPIX_EXPORT struct ImageRenderTarget {
    wgpu::Texture texture;
    float scale_factor = 1.0f;
};

EPIX_EXPORT struct NoColorTarget {
    glm::uvec2 size{0, 0};
};

/** @brief Stable identity of a normalized render target, used to share one
 * output attachment per target and to group cameras by target (Bevy
 * NormalizedRenderTarget as a HashMap key). */
EPIX_EXPORT struct RenderTargetId {
    /// The normalized RenderTarget alternative.  Keeping this separate from
    /// its value prevents a window entity, texture handle, and manual view
    /// with coincident numeric identities from sharing an attachment.
    std::uint8_t kind                   = 0;
    std::uint64_t value                 = 0;
    constexpr RenderTargetId() noexcept = default;
    constexpr RenderTargetId(std::uint64_t target_value) noexcept : value(target_value) {}
    constexpr RenderTargetId(std::uint8_t target_kind, std::uint64_t target_value) noexcept
        : kind(target_kind), value(target_value) {}
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
    std::size_t operator()(const RenderTargetId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value) ^ (std::hash<std::uint8_t>{}(id.kind) << 1);
    }
};

/** @brief Resolved target identity (Bevy `NormalizedRenderTarget`). Render
 * code uses this after an optional primary window has been resolved. */
EPIX_EXPORT struct NormalizedRenderTarget
    : std::variant<window::NormalizedWindowRef, ImageRenderTarget, ManualTextureViewHandle, NoColorTarget> {
    using std::variant<window::NormalizedWindowRef, ImageRenderTarget, ManualTextureViewHandle, NoColorTarget>::variant;
    RenderTargetId identity() const noexcept;
};

/** @brief A render target that is either a GPU texture or a window
 * reference (Bevy `bevy_camera::RenderTarget`). */
EPIX_EXPORT struct RenderTarget
    : std::variant<window::WindowRef, ImageRenderTarget, ManualTextureViewHandle, NoColorTarget> {
    using std::variant<window::WindowRef, ImageRenderTarget, ManualTextureViewHandle, NoColorTarget>::variant;
    static RenderTarget from_texture(wgpu::Texture texture, float scale_factor = 1.0f) {
        return RenderTarget(ImageRenderTarget{std::move(texture), scale_factor});
    }
    static RenderTarget from_primary() noexcept {
        return RenderTarget(window::WindowRef{window::WindowRef::Primary{}});
    }
    static RenderTarget from_window(epix::ecs::Entity window_entity) noexcept {
        return RenderTarget(window::WindowRef{window::WindowRef::Entity{window_entity}});
    }
    static RenderTarget from_manual_texture_view(ManualTextureViewHandle handle) noexcept {
        return RenderTarget(handle);
    }
    static RenderTarget none(glm::uvec2 size) noexcept { return RenderTarget(NoColorTarget{size}); }
    /** @brief Returns the image target when this is an image target (Bevy
     * `RenderTarget::as_image`). */
    const ImageRenderTarget* as_image() const noexcept { return std::get_if<ImageRenderTarget>(this); }
    std::optional<NormalizedRenderTarget> normalize(std::optional<epix::ecs::Entity> primary) const;
};

struct ComputedCameraValues {
    glm::mat4 clip_from_view{1.0f};
    std::optional<RenderTargetInfo> target_info;
    std::optional<glm::uvec2> old_viewport_size;
    std::optional<SubCameraView> old_sub_camera_view;
};

namespace detail {
struct CameraOutputModeWrite {
    std::optional<wgpu::BlendState> blend_state;
    ClearColorConfig clear_color{};
};
struct CameraOutputModeSkip {};
}  // namespace detail

/** @brief The final-output policy for a camera (Bevy `CameraOutputMode`). */
EPIX_EXPORT struct CameraOutputMode : std::variant<detail::CameraOutputModeWrite, detail::CameraOutputModeSkip> {
    using Write = detail::CameraOutputModeWrite;
    using Skip  = detail::CameraOutputModeSkip;

   private:
    using Base = std::variant<Write, Skip>;

   public:
    using Base::Base;
    CameraOutputMode() noexcept : Base(Write{}) {}
};

/** @brief Extra usages requested for a camera's intermediate main textures
 * (Bevy CameraMainTextureUsages). */
EPIX_EXPORT struct CameraMainTextureUsages {
    wgpu::TextureUsage usage =
        wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopySrc;

    /** @brief Returns a copy with additional usages, matching Bevy's
     * consuming `CameraMainTextureUsages::with` builder. */
    CameraMainTextureUsages with(wgpu::TextureUsage extra) const noexcept {
        auto result  = *this;
        result.usage = result.usage | extra;
        return result;
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
    /** @brief Whether this camera is active and should be used for rendering. */
    bool is_active = true;
    /** @brief Reverse face culling for mirrored views (Bevy
     * ExtractedView::invert_culling input). */
    bool invert_culling = false;
    /** @brief Final output policy (Bevy Camera::output_mode). */
    CameraOutputMode output_mode{};
    /** @brief MSAA writeback policy (Bevy Camera::msaa_writeback). */
    MsaaWriteback msaa_writeback = MsaaWriteback::Auto;
    /** @brief Optional slice of a larger, shared camera view. */
    std::optional<SubCameraView> sub_camera_view;

    /** @brief Computed values updated by camera systems. */
    ComputedCameraValues computed;
    /** @brief Clear color configuration for this camera. */
    ClearColorConfig clear_color{};

    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator);

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
        const auto min =
            viewport.transform([](const Viewport& vp) { return vp.physical_position; }).value_or(glm::uvec2(0, 0));
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
        if (viewport) return viewport->physical_size;
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
    const glm::mat4& clip_from_view() const noexcept { return computed.clip_from_view; }

    /** @brief Converts a world-space point to normalized device coordinates
     * (Bevy world_to_ndc). */
    std::optional<glm::vec3> world_to_ndc(const transform::GlobalTransform& camera_transform,
                                          glm::vec3 world_point) const noexcept {
        const glm::vec4 view_point = glm::inverse(camera_transform.matrix) * glm::vec4(world_point, 1.0f);
        const glm::vec4 clip_point = computed.clip_from_view * view_point;
        if (clip_point.w == 0.0f) return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip_point) / clip_point.w;
        return std::isfinite(ndc.x) && std::isfinite(ndc.y) && std::isfinite(ndc.z) ? std::optional<glm::vec3>(ndc)
                                                                                    : std::nullopt;
    }
    /** @brief Converts normalized device coordinates to world space (Bevy
     * ndc_to_world). */
    std::optional<glm::vec3> ndc_to_world(const transform::GlobalTransform& camera_transform,
                                          glm::vec3 ndc_point) const noexcept {
        const glm::vec4 view_point = glm::inverse(computed.clip_from_view) * glm::vec4(ndc_point, 1.0f);
        if (view_point.w == 0.0f) return std::nullopt;
        const glm::vec4 world_point = camera_transform.matrix * (view_point / view_point.w);
        const glm::vec3 result      = glm::vec3(world_point);
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
        ndc.y         = -ndc.y;
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
    std::expected<Ray3d, ViewportConversionError> viewport_to_world(const transform::GlobalTransform& camera_transform,
                                                                    glm::vec2 viewport_position) const noexcept {
        auto ndc_xy = viewport_to_ndc(viewport_position);
        if (!ndc_xy) return std::unexpected(ndc_xy.error());
        auto near_point = ndc_to_world(camera_transform, glm::vec3(*ndc_xy, 1.0f));
        auto far_point  = ndc_to_world(camera_transform, glm::vec3(*ndc_xy, std::numeric_limits<float>::epsilon()));
        if (!near_point || !far_point) return std::unexpected(ViewportConversionError::InvalidData);
        glm::vec3 direction = *far_point - *near_point;
        const float length  = glm::length(direction);
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
    float depth_ndc_to_view_z(float ndc_depth) const noexcept { return -computed.clip_from_view[3][2] / ndc_depth; }
    /** @brief Converts NDC depth to orthographic view Z (Bevy
     * depth_ndc_to_view_z_2d). */
    float depth_ndc_to_view_z_2d(float ndc_depth) const noexcept {
        return -(computed.clip_from_view[3][2] - ndc_depth) / computed.clip_from_view[2][2];
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
namespace detail {
struct Camera3dDepthLoadOpClear {
    float value = 0.0f;
};
struct Camera3dDepthLoadOpLoad {};
}  // namespace detail

EPIX_EXPORT struct Camera3dDepthLoadOp
    : std::variant<detail::Camera3dDepthLoadOpClear, detail::Camera3dDepthLoadOpLoad> {
    using Clear = detail::Camera3dDepthLoadOpClear;
    using Load  = detail::Camera3dDepthLoadOpLoad;

   private:
    using Base = std::variant<Clear, Load>;

   public:
    using Base::Base;
    constexpr Camera3dDepthLoadOp() noexcept : Base(Clear{}) {}
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
    std::size_t screen_space_specular_transmission_steps                      = 1;
    ScreenSpaceTransmissionQuality screen_space_specular_transmission_quality = ScreenSpaceTransmissionQuality::Medium;

    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<Camera>([] { return Camera{}; });
        registrator.template register_required<Projection>([] { return Projection{}; });
    }
};

/** @brief 2D camera marker (Bevy `Camera2d`).  Adding it supplies the
 * orthographic 2D projection and the standard camera requirements. */
EPIX_EXPORT struct Camera2d {
    static void register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
        registrator.template register_required<Camera>([] { return Camera{}; });
        registrator.template register_required<Projection>(
            [] { return Projection::orthographic(OrthographicProjection::default_2d()); });
        registrator.template register_required<Frustum>([] {
            const auto projection = OrthographicProjection::default_2d();
            return projection.compute_frustum(::epix::transform::GlobalTransform{});
        });
    }
};

struct PhysicalCameraParameters;

/** @brief Physical camera exposure in EV100 (Bevy Exposure). */
EPIX_EXPORT struct Exposure {
    static constexpr float EV100_SUNLIGHT = 15.0f;
    static constexpr float EV100_OVERCAST = 12.0f;
    static constexpr float EV100_INDOOR   = 7.0f;
    static constexpr float EV100_BLENDER  = 9.7f;

    static const Exposure SUNLIGHT;
    static const Exposure OVERCAST;
    static const Exposure INDOOR;
    static const Exposure BLENDER;
    static Exposure from_physical_camera(const PhysicalCameraParameters& parameters) noexcept;

    float ev100 = 9.7f;  ///< Blender-compatible Bevy default.
    float exposure() const noexcept { return std::exp2(-ev100) / 1.2f; }
};

inline const Exposure Exposure::SUNLIGHT{Exposure::EV100_SUNLIGHT};
inline const Exposure Exposure::OVERCAST{Exposure::EV100_OVERCAST};
inline const Exposure Exposure::INDOOR{Exposure::EV100_INDOOR};
inline const Exposure Exposure::BLENDER{Exposure::EV100_BLENDER};

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

/** @brief Structured failure from `camera_system` (Bevy's fallible camera
 * target resolution). */
EPIX_EXPORT struct CameraUpdateError : std::runtime_error {
    struct Window {
        epix::ecs::Entity entity;
    };
    struct Image {};

    std::variant<Window, Image> value;

    explicit CameraUpdateError(Window error)
        : std::runtime_error(std::format("Camera render target window {} is missing", error.entity.index)),
          value(error) {}
    explicit CameraUpdateError(Image error)
        : std::runtime_error("Camera render target image is missing"), value(error) {}
};

/** Update cameras when their render target, projection, viewport, or
 * sub-camera view changes (Bevy `camera_system`). Manual texture views are
 * resolved by the render module because that resource exists only there. */
EPIX_EXPORT std::expected<void, CameraUpdateError> camera_system(
    epix::ecs::EventReader<::epix::window::WindowResized> window_resized_reader,
    epix::ecs::EventReader<::epix::window::WindowCreated> window_created_reader,
    epix::ecs::EventReader<::epix::window::WindowScaleFactorChanged> window_scale_factor_changed_reader,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const ::epix::window::Window&>,
                     epix::ecs::With<::epix::window::PrimaryWindow>> primary_window_query,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const ::epix::window::Window&>> window_query,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Mut<Camera>,
                                     epix::ecs::Ref<RenderTarget>,
                                     epix::ecs::Mut<Projection>>> cameras);

/** @brief Plugin that registers camera_system (projection updates), the
 * visibility systems, the projection plugins, and the ClearColor resource
 * (Bevy bevy_camera::CameraPlugin — the user-facing, main-world camera
 * plugin; render-world extraction lives in the render module). */
EPIX_EXPORT struct CameraPlugin {
    void attach(epix::app::App& app);
};

}  // namespace epix::camera

/** @brief Hash support for the camera-owned ManualTextureViewHandle. */
template <>
struct std::hash<epix::camera::ManualTextureViewHandle> {
    std::size_t operator()(const epix::camera::ManualTextureViewHandle& handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.id);
    }
};

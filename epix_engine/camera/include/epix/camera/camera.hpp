#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
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
    glm::uvec2 pos;
    /** @brief Size of the viewport in pixels. */
    glm::uvec2 size;
    /** @brief Depth range for the viewport (min, max). */
    std::pair<float, float> depth_range{0.0f, 1.0f};
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
    glm::mat4 projection;
    glm::uvec2 target_size;
    std::optional<glm::uvec2> old_viewport_size;
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

// --- Camera Systems --- //

/** @brief System labels for camera update systems (Bevy CameraUpdateSystems). */
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
        // update the stored target size, update the projection if needed.

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

/** @brief Plugin that registers camera_system (projection updates), the
 * visibility systems, the projection plugins, and the ClearColor resource
 * (Bevy bevy_camera::CameraPlugin — the user-facing, main-world camera
 * plugin; render-world extraction lives in the render module). */
EPIX_EXPORT struct CameraPlugin {
    void attach(epix::app::App& app);
};

}  // namespace epix::camera

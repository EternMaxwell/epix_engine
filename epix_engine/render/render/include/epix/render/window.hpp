#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/window.hpp>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::camera {
/** @brief Forward declaration (window.hpp is included by view.hpp). */
struct ExtractedCamera;
}  // namespace epix::render::camera
namespace epix::render::view {
/** @brief Forward declaration (window.hpp is included by view.hpp). */
struct ViewTarget;
}  // namespace epix::render::view

namespace epix::render::window {
/**
 * @brief A component for window entity, to tell how its surface should be created, cause the backend is unknown.
 */
EPIX_EXPORT using SurfaceCreation = std::function<wgpu::Surface(const wgpu::Instance&)>;
/** @brief Snapshot of a window's rendering parameters extracted into the
 * render world each frame. */
EPIX_EXPORT struct ExtractedWindow {
    /** @brief Entity ID of the source window. */
    ecs::Entity entity;
    /** @brief Functor to create a surface from a wgpu::Instance. */
    SurfaceCreation create_surface;
    /** @brief Window width in physical pixels. */
    int physical_width;
    /** @brief Window height in physical pixels. */
    int physical_height;
    /** @brief Requested present mode. */
    epix::window::PresentMode present_mode;
    /** @brief Requested composite alpha mode. */
    epix::window::CompositeAlphaMode alpha_mode;

    /** @brief Texture view of the current swapchain frame. */
    wgpu::TextureView swapchain_texture_view;
    /** @brief Surface texture of the current swapchain frame. */
    wgpu::SurfaceTexture swapchain_texture;
    /** @brief Texture format of the swapchain surface. */
    wgpu::TextureFormat swapchain_texture_format;
    /** @brief Format of the swapchain texture VIEW: the sRGB-suffixed twin of
     * the surface format so the final output attachment always writes through
     * hardware sRGB encoding (Bevy `swap_chain_texture_view_format`). */
    wgpu::TextureFormat swapchain_texture_view_format;

    /** @brief Whether the window size changed since last frame. */
    bool size_changed = false;
    /** @brief Whether the present mode changed since last frame. */
    bool present_mode_changed = false;
    /** @brief Present at least once, even before any camera rendered (Bevy
     * ExtractedWindow::needs_initial_present). */
    bool needs_initial_present = true;

    /** @brief Whether a swapchain texture is currently held (Bevy
     * ExtractedWindow::has_swapchain_texture). */
    bool has_swapchain_texture() const noexcept {
        return static_cast<bool>(swapchain_texture_view) &&
               (swapchain_texture.status == wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal ||
                swapchain_texture.status == wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal);
    }
    /** @brief Release the held swapchain texture after presenting (Bevy
     * ExtractedWindow::present takes the Option). */
    void release_swapchain_texture() {
        swapchain_texture      = wgpu::SurfaceTexture{};
        swapchain_texture_view = nullptr;
    }
};
/** @brief Resource collecting all extracted windows for the current
 * frame. */
EPIX_EXPORT struct ExtractedWindows {
    ExtractedWindows()                                   = default;
    ExtractedWindows(const ExtractedWindows&)            = delete;
    ExtractedWindows& operator=(const ExtractedWindows&) = delete;
    ExtractedWindows(ExtractedWindows&&)                 = default;
    ExtractedWindows& operator=(ExtractedWindows&&)      = default;

    std::optional<ecs::Entity> primary;
    std::unordered_map<ecs::Entity, ExtractedWindow> windows;
};
struct SurfaceData {
    SurfaceData(wgpu::Surface surface, wgpu::SurfaceConfiguration config)
        : surface(std::move(surface)), config(std::move(config)) {}
    SurfaceData(const SurfaceData&)            = delete;
    SurfaceData& operator=(const SurfaceData&) = delete;
    SurfaceData(SurfaceData&&)                 = default;
    SurfaceData& operator=(SurfaceData&&)      = default;

    wgpu::Surface surface;
    wgpu::SurfaceConfiguration config;
};
struct WindowSurfaces {
    WindowSurfaces()                                 = default;
    WindowSurfaces(const WindowSurfaces&)            = delete;
    WindowSurfaces& operator=(const WindowSurfaces&) = delete;
    WindowSurfaces(WindowSurfaces&&)                 = default;
    WindowSurfaces& operator=(WindowSurfaces&&)      = default;

    std::unordered_map<ecs::Entity, SurfaceData> surfaces;
    std::unordered_set<ecs::Entity> configured_windows;

    void remove(const ecs::Entity& entity);
};

/**
 * @brief System for extracting windows.
 */
EPIX_EXPORT void extract_windows(ecs::ResMut<ExtractedWindows> extracted_windows,
                                 app::Extract<ecs::Query<ecs::Item<ecs::Entity,
                                                                   const epix::window::Window&,
                                                                   const SurfaceCreation&,
                                                                   ecs::Has<epix::window::PrimaryWindow>>>> windows,
                                 ecs::ResMut<WindowSurfaces> window_surfaces,
                                 app::Extract<ecs::EventReader<epix::window::WindowClosed>> closed);
/**
 * @brief System for making swapchain texture and texture view available.
 */
EPIX_EXPORT void prepare_windows(ecs::ResMut<ExtractedWindows> windows,
                                 ecs::ResMut<WindowSurfaces> window_surfaces,
                                 ecs::Res<wgpu::Device> device,
                                 ecs::Res<wgpu::Instance> instance);
void create_surfaces(ecs::ResMut<ExtractedWindows> windows,
                     ecs::ResMut<WindowSurfaces> window_surfaces,
                     ecs::Res<wgpu::Instance> instance,
                     ecs::Res<wgpu::Adapter> adapter,
                     ecs::Res<wgpu::Device> device);

/** @brief Present completed surface frames. Called by render_system immediately
 * after graph submission, matching Bevy renderer::render_system. */
void present_windows(ecs::World& world);

/** @brief Plugin that registers window surface creation, extraction,
 * preparation, and presentation systems. */
EPIX_EXPORT struct WindowRenderPlugin {
    void attach(app::App&);
};
}  // namespace epix::render::window

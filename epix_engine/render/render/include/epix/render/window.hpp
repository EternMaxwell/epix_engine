#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <epix/core.hpp>
#include <epix/window.hpp>
#include <webgpu/webgpu.hpp>
namespace epix::render::window {
/**
 * @brief A component for window entity, to tell how its surface should be created, cause the backend is unknown.
 */
using SurfaceCreation = std::function<wgpu::Surface(const wgpu::Instance&)>;
/** @brief Snapshot of a window's rendering parameters extracted into the
 * render world each frame. */
struct ExtractedWindow {
    /** @brief epix::core::EntityID of the source window. */
    epix::core::Entity entity;
    /** @brief Functor to create a surface from a wgpu::Instance. */
    SurfaceCreation create_surface;
    /** @brief epix::window::Window width in physical pixels. */
    int physical_width;
    /** @brief epix::window::Window height in physical pixels. */
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

    /** @brief Whether the window size changed since last frame. */
    bool size_changed = false;
    /** @brief Whether the present mode changed since last frame. */
    bool present_mode_changed = false;
};
/** @brief Resource collecting all extracted windows for the current
 * frame. */
struct ExtractedWindows {
    ExtractedWindows()                                   = default;
    ExtractedWindows(const ExtractedWindows&)            = delete;
    ExtractedWindows& operator=(const ExtractedWindows&) = delete;
    ExtractedWindows(ExtractedWindows&&)                 = default;
    ExtractedWindows& operator=(ExtractedWindows&&)      = default;

    std::optional<epix::core::Entity> primary;
    std::unordered_map<epix::core::Entity, ExtractedWindow> windows;
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

    std::unordered_map<epix::core::Entity, SurfaceData> surfaces;
    std::unordered_set<epix::core::Entity> configured_windows;

    void remove(const epix::core::Entity& entity);
};

/**
 * @brief System for extracting windows.
 */
void extract_windows(
    epix::core::ResMut<ExtractedWindows> extracted_windows,
    epix::core::Extract<epix::core::Query<epix::core::Item<epix::core::Entity, const epix::window::Window&, const epix::render::window::SurfaceCreation&, epix::core::Has<epix::window::PrimaryWindow>>>> windows,
    epix::core::ResMut<WindowSurfaces> window_surfaces,
    epix::core::Extract<epix::core::EventReader<epix::window::WindowClosed>> closed);
/**
 * @brief System for making swapchain texture and texture view available.
 */
void prepare_windows(epix::core::ResMut<ExtractedWindows> windows,
                            epix::core::ResMut<WindowSurfaces> window_surfaces,
                            epix::core::Res<wgpu::Device> device,
                            epix::core::Res<wgpu::Instance> instance);
void create_surfaces(epix::core::Res<ExtractedWindows> windows,
                     epix::core::ResMut<WindowSurfaces> window_surfaces,
                     epix::core::Res<wgpu::Instance> instance,
                     epix::core::Res<wgpu::Adapter> adapter,
                     epix::core::Res<wgpu::Device> device);

void present_windows(epix::core::ResMut<WindowSurfaces> window_surfaces, epix::core::ResMut<ExtractedWindows> windows);

/** @brief Plugin that registers window surface creation, extraction,
 * preparation, and presentation systems. */
struct WindowRenderPlugin {
    /** @brief Whether this plugin handles presenting the swapchain
     * (default true). */
    bool handle_present = true;
    void attach(epix::core::App&);
};
}  // namespace epix::render::window
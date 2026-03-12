export module epix.render:window;

import epix.core;
import epix.window;
import webgpu;
import std;

using namespace core;
using namespace window;

namespace render::window {
/**
 * @brief A component for window entity, to tell how its surface should be created, cause the backend is unknown.
 */
export using SurfaceCreation = std::function<wgpu::Surface(const wgpu::Instance&)>;
/** @brief Snapshot of a window's rendering parameters extracted into the
 * render world each frame. */
export struct ExtractedWindow {
    /** @brief Entity ID of the source window. */
    Entity entity;
    /** @brief Functor to create a surface from a wgpu::Instance. */
    SurfaceCreation create_surface;
    /** @brief Window width in physical pixels. */
    int physical_width;
    /** @brief Window height in physical pixels. */
    int physical_height;
    /** @brief Requested present mode. */
    PresentMode present_mode;
    /** @brief Requested composite alpha mode. */
    CompositeAlphaMode alpha_mode;

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
export struct ExtractedWindows {
    ExtractedWindows()                                   = default;
    ExtractedWindows(const ExtractedWindows&)            = delete;
    ExtractedWindows& operator=(const ExtractedWindows&) = delete;
    ExtractedWindows(ExtractedWindows&&)                 = default;
    ExtractedWindows& operator=(ExtractedWindows&&)      = default;

    std::optional<Entity> primary;
    std::unordered_map<Entity, ExtractedWindow> windows;
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

    std::unordered_map<Entity, SurfaceData> surfaces;
    std::unordered_set<Entity> configured_windows;

    void remove(const Entity& entity);
};

/**
 * @brief System for extracting windows.
 */
export void extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Extract<Query<Item<Entity, const Window&, const SurfaceCreation&, Has<PrimaryWindow>>>> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Extract<EventReader<WindowClosed>> closed);
/**
 * @brief System for making swapchain texture and texture view available.
 */
export void prepare_windows(ResMut<ExtractedWindows> windows,
                            ResMut<WindowSurfaces> window_surfaces,
                            Res<wgpu::Device> device,
                            Res<wgpu::Instance> instance);
void create_surfaces(Res<ExtractedWindows> windows,
                     ResMut<WindowSurfaces> window_surfaces,
                     Res<wgpu::Instance> instance,
                     Res<wgpu::Adapter> adapter,
                     Res<wgpu::Device> device);

void present_windows(ResMut<WindowSurfaces> window_surfaces, ResMut<ExtractedWindows> windows);

/** @brief Plugin that registers window surface creation, extraction,
 * preparation, and presentation systems. */
export struct WindowRenderPlugin {
    /** @brief Whether this plugin handles presenting the swapchain
     * (default true). */
    bool handle_present = true;
    void build(App&);
};
}  // namespace render::window
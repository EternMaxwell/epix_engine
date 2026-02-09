export module epix.render:window;

import epix.core;
import epix.window;
import webgpu;
import std;

using namespace core;
using namespace window;

namespace render::window {
struct ExtractedWindow {
    Entity entity;
    std::function<wgpu::Surface(const wgpu::Instance&)> create_surface;
    int physical_width;
    int physical_height;
    PresentMode present_mode;
    CompositeAlphaMode alpha_mode;

    wgpu::TextureView swapchain_texture_view;
    wgpu::SurfaceTexture swapchain_texture;
    wgpu::TextureFormat swapchain_texture_format;

    bool size_changed         = false;
    bool present_mode_changed = false;

    static ExtractedWindow from_window(const Entity& entity, const Window& window);
};
struct ExtractedWindows {
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
 * @brief System for updating extracted windows from actual windows.
 * This system does not count for newly created windows, they should be extracted by window backend systems.
 * Exported so it can be used as `SystemSetLabel` for configuring system dependencies.
 */
export void update_extracted(ResMut<ExtractedWindows> extracted_windows,
                             Extract<Query<Item<Entity, const Window&, Has<PrimaryWindow>>>> windows,
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

struct WindowRenderPlugin {
    bool handle_present = true;
    void build(App&);
};
}  // namespace render::window
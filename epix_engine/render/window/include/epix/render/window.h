#pragma once

#include <epix/app.h>
#include <epix/utils/core.h>
#include <epix/wgpu.h>
#include <epix/window.h>

namespace epix::render::window {
struct ExtractedWindow {
    Entity entity;
    GLFWwindow* handle;
    int physical_width;
    int physical_height;
    epix::window::PresentMode present_mode;
    epix::window::CompositeAlphaMode alpha_mode;

    wgpu::TextureView swapchain_texture_view;
    wgpu::SurfaceTexture swapchain_texture;
    wgpu::TextureFormat swapchain_texture_format;

    bool size_changed         = false;
    bool present_mode_changed = false;

    EPIX_API void set_swapchain_texture(wgpu::SurfaceTexture texture);
};
struct ExtractedWindows {
    std::optional<Entity> primary;
    entt::dense_map<Entity, ExtractedWindow> windows;
};
struct SurfaceData {
    wgpu::Surface surface;
    wgpu::SurfaceConfiguration config;
};
struct WindowSurfaces {
    entt::dense_map<Entity, SurfaceData> surfaces;
    entt::dense_set<Entity> configured;

    EPIX_API void remove(const Entity& entity);
};

EPIX_API void extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Extract<EventReader<epix::window::WindowClosed>> closed,
    Extract<Query<
        Get<Entity, epix::window::Window, Has<epix::window::PrimaryWindow>>>>
        windows,
    Extract<Res<glfw::GLFWwindows>> glfw_windows,
    ResMut<WindowSurfaces> window_surfaces
);
EPIX_API void create_surfaces(
    Res<ExtractedWindows> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Res<wgpu::Instance> instance,
    Res<wgpu::Adapter> adapter,
    Res<wgpu::Device> device
);

EPIX_API void prepare_windows(
    ResMut<ExtractedWindows> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Res<wgpu::Device> device,
    Res<wgpu::Adapter> adapter
);

EPIX_API void present_windows(ResMut<WindowSurfaces> window_surfaces);

struct WindowRenderPlugin : public epix::Plugin {
    bool handle_present = true;
    EPIX_API void build(epix::App&) override;
};
}  // namespace epix::render::window
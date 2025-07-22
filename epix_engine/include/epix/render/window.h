#pragma once

#include <epix/app.h>
#include <epix/utils/core.h>
#include <epix/vulkan.h>
#include <epix/window.h>

namespace epix::render::window {
struct ExtractedWindow {
    Entity entity;
    GLFWwindow* handle;
    int physical_width;
    int physical_height;
    epix::window::PresentMode present_mode;
    epix::window::CompositeAlphaMode alpha_mode;

    nvrhi::TextureHandle swapchain_texture;

    bool size_changed         = false;
    bool present_mode_changed = false;
};
struct ExtractedWindows {
    std::optional<Entity> primary;
    entt::dense_map<Entity, ExtractedWindow> windows;
};
struct SurfaceData {
    vk::Device device;
    vk::Instance instance;
    vk::SurfaceKHR surface;
    nvrhi::Format image_format;
    vk::SwapchainKHR swapchain;
    vk::SwapchainCreateInfoKHR config;
    std::vector<nvrhi::TextureHandle> swapchain_images;
    std::vector<vk::Fence> swapchain_image_fences;
    uint32_t fence_index         = 0;
    uint32_t current_image_index = 0;
};
struct WindowSurfaces {
    entt::dense_map<Entity, SurfaceData> surfaces;

    EPIX_API void remove(const Entity& entity);
};

EPIX_API void extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Res<vk::Device> device,
    Extract<EventReader<epix::window::WindowClosed>> closed,
    Extract<Query<
        Get<Entity, epix::window::Window, Has<epix::window::PrimaryWindow>>>>
        windows,
    Extract<Res<glfw::GLFWwindows>> glfw_windows,
    ResMut<WindowSurfaces> window_surfaces);
EPIX_API void create_surfaces(Res<ExtractedWindows> windows,
                              ResMut<WindowSurfaces> window_surfaces,
                              Res<vk::Instance> instance,
                              Res<vk::PhysicalDevice> physical_device,
                              Res<vk::Device> device,
                              Res<vk::Queue> queue,
                              Res<nvrhi::DeviceHandle> nvrhi_device,
                              Res<render::CommandPools> command_pools,
                              Local<vk::CommandBuffer> pcmd_buffer);

EPIX_API void prepare_windows(ResMut<ExtractedWindows> windows,
                              ResMut<WindowSurfaces> window_surfaces,
                              Res<vk::Device> device,
                              Res<vk::PhysicalDevice> physical_device,
                              Res<nvrhi::DeviceHandle> nvrhi_device);

EPIX_API void present_windows(ResMut<WindowSurfaces> window_surfaces,
                              Res<vk::Queue> queue);

struct WindowRenderPlugin {
    bool handle_present = true;
    EPIX_API void build(epix::App&);
};
}  // namespace epix::render::window
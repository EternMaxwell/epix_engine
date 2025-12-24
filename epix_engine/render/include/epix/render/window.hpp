#pragma once

#include <epix/core.hpp>

#include "vulkan.hpp"

// vulkan before glfw to enable glfw vulkan functionalities
#include <epix/glfw.hpp>
#include <epix/window.hpp>

namespace epix::render::window {
struct ExtractedWindow {
    Entity entity;
    GLFWwindow* handle;
    int physical_width;
    int physical_height;
    epix::window::PresentMode present_mode;
    epix::window::CompositeAlphaMode alpha_mode;

    nvrhi::TextureHandle swapchain_texture;

    bool valid                = true;
    bool size_changed         = false;
    bool present_mode_changed = false;
};
struct ExtractedWindows {
    std::optional<Entity> primary;
    std::unordered_map<Entity, ExtractedWindow> windows;
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
    std::unordered_map<Entity, SurfaceData> surfaces;

    void remove(const Entity& entity);
};

void extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Res<vk::Device> device,
    Extract<EventReader<epix::window::WindowClosed>> closed,
    Extract<Query<Item<Entity, const epix::window::Window&, Has<epix::window::PrimaryWindow>>>> windows,
    Extract<Res<glfw::GLFWwindows>> glfw_windows,
    ResMut<WindowSurfaces> window_surfaces);
void create_surfaces(Res<ExtractedWindows> windows,
                     ResMut<WindowSurfaces> window_surfaces,
                     Res<vk::Instance> instance,
                     Res<vk::PhysicalDevice> physical_device,
                     Res<vk::Device> device,
                     Res<vk::Queue> queue,
                     Res<nvrhi::DeviceHandle> nvrhi_device);
void prepare_windows(ResMut<ExtractedWindows> windows,
                     ResMut<WindowSurfaces> window_surfaces,
                     Res<vk::Device> device,
                     Res<vk::PhysicalDevice> physical_device,
                     Res<nvrhi::DeviceHandle> nvrhi_device);

void present_windows(ResMut<WindowSurfaces> window_surfaces, Res<vk::Queue> queue);

struct WindowRenderPlugin {
    bool handle_present = true;
    void build(epix::App&);
};
}  // namespace epix::render::window
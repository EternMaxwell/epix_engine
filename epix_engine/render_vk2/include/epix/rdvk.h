#pragma once

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>
#include <epix/app.h>
#include <epix/common.h>
#include <epix/window.h>
#include <spdlog/spdlog.h>
#include <vk_mem_alloc.h>

#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace epix::render::vulkan2::backend {
using vk::Result;

struct Instance;
using vk::PhysicalDevice;
struct Device;
using vk::Event;
using vk::Fence;
using vk::QueryPool;
using vk::Queue;
using vk::Semaphore;
struct Buffer;
using vk::BufferView;
struct Image;
using vk::CommandBuffer;
using vk::CommandPool;
using vk::DescriptorPool;
using vk::DescriptorSet;
using vk::DescriptorSetLayout;
using vk::Framebuffer;
using vk::ImageView;
using vk::Pipeline;
using vk::PipelineCache;
using vk::PipelineLayout;
using vk::RenderPass;
using vk::Sampler;
using vk::ShaderModule;
struct Surface;
struct Swapchain;

struct AllocationCreateInfo;

using vk::ApplicationInfo;
struct Instance {
    vk::Instance instance;
    vk::DebugUtilsMessengerEXT debug_messenger;
    std::shared_ptr<spdlog::logger> logger;

    EPIX_API vk::Instance& operator*();

    EPIX_API static Instance create(
        vk::ApplicationInfo app_info, std::shared_ptr<spdlog::logger> logger
    );
    EPIX_API static Instance create(
        const char* app_name,
        uint32_t app_version,
        std::shared_ptr<spdlog::logger> logger
    );
    EPIX_API void destroy();

    EPIX_API std::vector<vk::PhysicalDevice> enumerate_physical_devices();
    EPIX_API std::vector<vk::PhysicalDeviceGroupProperties>
    enumerate_physical_device_groups();
};
struct Device : vk::Device {
    VmaAllocator allocator;
    uint32_t queue_family_index;

    Instance instance;
    PhysicalDevice physical_device;

    EPIX_API operator bool() const;

    EPIX_API void operator=(const vk::Device& device);

    EPIX_API static Device create(
        Instance& instance,
        PhysicalDevice& physical_device,
        vk::QueueFlags queue_flags = vk::QueueFlagBits::eGraphics
    );
    EPIX_API void destroy();

    EPIX_API Buffer create_buffer(
        vk::BufferCreateInfo& create_info, AllocationCreateInfo& alloc_info
    );
    EPIX_API void destroy_buffer(Buffer& buffer);
    EPIX_API void destroy(Buffer& buffer);

    EPIX_API Image create_image(
        vk::ImageCreateInfo& create_info, AllocationCreateInfo& alloc_info
    );
    EPIX_API void destroy_image(Image& image);
    EPIX_API void destroy(Image& image);
};
struct Image {
    Device device;
    VmaAllocation allocation;
    vk::Image image;

    EPIX_API vk::Image& operator*();
    EPIX_API operator bool() const;

    EPIX_API static Image create(
        Device& device,
        vk::ImageCreateInfo& create_info,
        AllocationCreateInfo& alloc_info
    );
    EPIX_API void destroy();

    EPIX_API vk::SubresourceLayout get_subresource_layout(
        const vk::ImageSubresource& subresource
    );
};
struct Buffer {
    Device device;
    VmaAllocation allocation;
    vk::Buffer buffer;

    EPIX_API vk::Buffer& operator*();
    EPIX_API operator bool() const;

    EPIX_API static Buffer create(
        Device& device,
        vk::BufferCreateInfo& create_info,
        AllocationCreateInfo& alloc_info
    );
    EPIX_API static Buffer create_device(
        Device& device, uint64_t size, vk::BufferUsageFlags usage
    );
    EPIX_API static Buffer create_device_dedicated(
        Device& device, uint64_t size, vk::BufferUsageFlags usage
    );
    EPIX_API static Buffer create_host(
        Device& device, uint64_t size, vk::BufferUsageFlags usage
    );
    EPIX_API void destroy();

    EPIX_API void* map();
    EPIX_API void unmap();
};
struct AllocationCreateInfo {
    EPIX_API AllocationCreateInfo();
    EPIX_API AllocationCreateInfo(
        const VmaMemoryUsage& usage, const VmaAllocationCreateFlags& flags
    );

    EPIX_API operator VmaAllocationCreateInfo() const;
    EPIX_API AllocationCreateInfo& setUsage(VmaMemoryUsage usage);
    EPIX_API AllocationCreateInfo& setFlags(VmaAllocationCreateFlags flags);
    EPIX_API AllocationCreateInfo& setRequiredFlags(VkMemoryPropertyFlags flags
    );
    EPIX_API AllocationCreateInfo& setPreferredFlags(VkMemoryPropertyFlags flags
    );
    EPIX_API AllocationCreateInfo& setMemoryTypeBits(uint32_t bits);
    EPIX_API AllocationCreateInfo& setPool(VmaPool pool);
    EPIX_API AllocationCreateInfo& setPUserData(void* data);
    EPIX_API AllocationCreateInfo& setPriority(float priority);
    EPIX_API VmaAllocationCreateInfo& operator*();

    VmaAllocationCreateInfo create_info;
};
struct Surface {
    Instance instance;
    vk::SurfaceKHR surface;

    EPIX_API vk::SurfaceKHR& operator*();
    EPIX_API operator bool() const;

    EPIX_API static Surface create(Instance& device, GLFWwindow* window);
    EPIX_API void destroy();
};
struct Swapchain {
    Device device;
    Surface surface;
    vk::SurfaceFormatKHR surface_format;
    vk::PresentModeKHR present_mode;
    vk::Fence in_flight_fence[2];
    struct Others {
        vk::SwapchainKHR swapchain;
        std::vector<vk::Image> images;
        std::vector<ImageView> image_views;
        vk::Extent2D extent;
        uint32_t image_index   = 0;
        uint32_t current_frame = 0;
    };
    std::shared_ptr<Others> others;

    EPIX_API static Swapchain create(
        Device& device, Surface& surface, bool vsync = false
    );
    EPIX_API void destroy();
    EPIX_API void recreate();
    EPIX_API vk::Image next_image();
    EPIX_API vk::Image current_image() const;
    EPIX_API ImageView current_image_view() const;
    EPIX_API vk::Fence fence() const;
};
}  // namespace epix::render::vulkan2::backend

namespace epix::render::vulkan2 {
struct RenderContext {};
struct RenderVKPlugin;
struct ContextCommandBuffer {};
namespace systems {
using namespace epix::render::vulkan2::backend;
using epix::Command;
using epix::Extract;
using epix::Get;
using epix::Query;
using epix::Res;
using epix::ResMut;
using epix::With;
using epix::Without;
using window::components::PrimaryWindow;
using window::components::Window;
EPIX_API void create_context(
    Command cmd, Query<Get<Window>, With<PrimaryWindow>> query
);
EPIX_API void destroy_context(
    Command cmd,
    Query<
        Get<Instance, Device, Surface, Swapchain, CommandPool>,
        With<RenderContext>> query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
);
EPIX_API void extract_context(
    Extract<
        Get<Instance, Device, Surface, Swapchain, CommandPool, Queue>,
        With<RenderContext>> query,
    Extract<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query,
    Command cmd
);
EPIX_API void clear_extracted_context(
    Query<Get<Entity>, With<RenderContext>> query,
    Query<Get<Entity>, With<ContextCommandBuffer>> cmd_query,
    Command cmd
);
EPIX_API void recreate_swap_chain(
    Query<Get<Swapchain>, With<RenderContext>> query
);
EPIX_API void get_next_image(
    Query<Get<Device, Swapchain, CommandPool, Queue>, With<RenderContext>>
        query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
);
EPIX_API void present_frame(
    Query<Get<Swapchain, Queue, Device, CommandPool>, With<RenderContext>>
        query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
);
}  // namespace systems
struct RenderVKPlugin : public epix::Plugin {
    EPIX_API void build(epix::App& app) override;
};
}  // namespace epix::render::vulkan2

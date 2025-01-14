#pragma once

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>
#include <epix/common.h>
#include <epix/vulkan.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <spirv_glsl.hpp>
#include <vector>

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
        vk::ApplicationInfo app_info,
        std::shared_ptr<spdlog::logger> logger,
        bool debug = false
    );
    EPIX_API static Instance create(
        const char* app_name,
        uint32_t app_version,
        std::shared_ptr<spdlog::logger> logger,
        bool debug = false
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

    EPIX_API Buffer createBuffer(
        vk::BufferCreateInfo& create_info, AllocationCreateInfo& alloc_info
    );
    EPIX_API void destroyBuffer(Buffer& buffer);
    EPIX_API void destroy(Buffer& buffer);

    EPIX_API Image createImage(
        vk::ImageCreateInfo& create_info, AllocationCreateInfo& alloc_info
    );
    EPIX_API void destroyImage(Image& image);
    EPIX_API void destroy(Image& image);
};
struct Image {
    Device device            = {};
    VmaAllocation allocation = {};
    vk::Image image;

    EPIX_API vk::Image& operator*();
    EPIX_API operator bool() const;
    operator vk::Image() const { return image; }
    Image() = default;
    Image(const vk::Image& img) : image(img) {}

    Image& operator=(const vk::Image& img) {
        image = img;
        return *this;
    }

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
    Device device            = {};
    VmaAllocation allocation = {};
    vk::Buffer buffer;

    EPIX_API vk::Buffer& operator*();
    EPIX_API operator bool() const;
    operator vk::Buffer() const { return buffer; }
    Buffer() = default;
    Buffer(const vk::Buffer& buf) : buffer(buf) {}

    Buffer& operator=(const vk::Buffer& buf) {
        buffer = buf;
        return *this;
    }

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
    bool need_transition = true;
    vk::SurfaceFormatKHR surface_format;
    vk::PresentModeKHR present_mode;
    mutable vk::Fence in_flight_fence[2];
    struct Others {
        vk::SwapchainKHR swapchain;
        std::vector<vk::Image> images;
        mutable std::vector<ImageView> image_views;
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
    EPIX_API Image next_image();
    EPIX_API Image current_image() const;
    EPIX_API ImageView& current_image_view() const;
    EPIX_API vk::Fence& fence() const;

    EPIX_API void transition_image_layout(
        CommandBuffer& command_buffer, Fence& fence
    );
};
}  // namespace epix::render::vulkan2::backend
#pragma once

#include "rdvk_basic.h"

namespace epix::render::vulkan2 {
namespace node {
template <typename ResT, typename StateT>
struct ResourceHandle {
    struct node {
        ResT resource;
        StateT state;
    };
    std::shared_ptr<node> data;
    ResourceHandle() = default;
    ResourceHandle(ResT res, StateT state)
        : data(std::make_shared<node>({res, state})) {}
    operator bool() const { return data != nullptr; }
    ResT& operator*() { return data->resource; }
    StateT& state() { return data->state; }
    ResT& resource() { return data->resource; }
};
struct VulkanContext_T {
    backend::Instance instance;
    backend::PhysicalDevice physical_device;
    backend::Device device;
    backend::Queue queue;
    backend::CommandPool command_pool;
    backend::Surface primary_surface;
    backend::Swapchain primary_swapchain;
};
struct ContextCmd_T {
    backend::CommandBuffer cmd_buffer;
    backend::Fence fence;
};
using VulkanContext = ResourceHandle<VulkanContext_T, void>;
using ContextCmd = ResourceHandle<ContextCmd_T, void>;

struct BufferState {
    vk::BufferUsageFlags usage;
    vk::PipelineStageFlags stage;
    vk::AccessFlags access;
};
using Buffer_T = backend::Buffer;
using BufferHandle = ResourceHandle<backend::Buffer, BufferState>;
struct ImageState {
    vk::ImageUsageFlags usage;
    vk::PipelineStageFlags stage;
    vk::AccessFlags access;
    vk::ImageLayout layout;
};
struct Image_T {
    backend::Image image;
    backend::ImageView view;
};
using ImageHandle = ResourceHandle<backend::Image, ImageState>;
}  // namespace node
}  // namespace epix::render::vulkan2
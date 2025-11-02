#pragma once

#include <epix/core.hpp>

// include volk after core.hpp
#include <volk.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
// include vulkan before volk
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

template <>
struct epix::core::copy_ref<nvrhi::DeviceHandle> : public std::true_type {};

namespace epix::render {
struct LocalCommandList {
    nvrhi::CommandListHandle handle;
    static LocalCommandList from_world(World& world) {
        if (auto device = world.get_resource<nvrhi::DeviceHandle>()) {
            auto handle = device.value().get();
            return LocalCommandList{
                .handle = handle->createCommandList(nvrhi::CommandListParameters{}.setEnableImmediateExecution(false)),
            };
        } else {
            throw std::runtime_error("Failed to get nvrhi::DeviceHandle from world for LocalCommandList");
        }
    }
};
static_assert(core::is_from_world<LocalCommandList>);
nvrhi::DeviceHandle create_async_device(nvrhi::DeviceHandle device);
}  // namespace epix::render
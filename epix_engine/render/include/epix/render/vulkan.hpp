#pragma once

#include <epix/core.hpp>

// include volk after core.hpp, cause it will try to introduce macros that conflict with stl.
#ifdef EPIX_USE_VOLK
#include <volk.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
#include <vulkan/vulkan.hpp>
// include vulkan before volk
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

template <>
struct epix::core::copy_ref<nvrhi::DeviceHandle> : public std::true_type {};

namespace epix::render {
struct LocalCommandList {
    nvrhi::CommandListHandle handle;
    static LocalCommandList from_world(World& world);
};
static_assert(core::is_from_world<LocalCommandList>);
nvrhi::DeviceHandle create_async_device(nvrhi::DeviceHandle device);
}  // namespace epix::render
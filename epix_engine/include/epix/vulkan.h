#pragma once

#ifdef EPIX_USE_VOLK
#include <volk.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#endif
#include <vulkan/vulkan.hpp>
// include vulkan before volk
#include <epix/app.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

template <>
inline constexpr bool epix::app::copy_res<nvrhi::DeviceHandle> = true;

namespace epix::render {
struct CommandPools {
   private:
    vk::Device device;
    uint32_t queue_family_index;
    mutable entt::dense_map<std::thread::id, vk::CommandPool> pools;
    mutable std::shared_mutex mutex;

    void destroy() {
        std::unique_lock lock(mutex);
        for (auto&& [thread_id, pool] : pools) {
            if (pool) {
                device.destroyCommandPool(pool);
            }
        }
        pools.clear();
    }

   public:
    CommandPools(vk::Device device, uint32_t queue_family_index)
        : device(device), queue_family_index(queue_family_index) {}
    vk::Device get_device() const { return device; }
    vk::CommandPool get() const {
        std::shared_lock lock(mutex);
        auto thread_id = std::this_thread::get_id();
        if (pools.contains(thread_id)) {
            return pools.at(thread_id);
        }
        lock.unlock();  // unlock before creating a new pool
        auto create_info = vk::CommandPoolCreateInfo()
                               .setQueueFamilyIndex(queue_family_index)
                               .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        vk::CommandPool pool = device.createCommandPool(create_info);
        std::unique_lock unique_lock(mutex);
        pools.emplace(thread_id, pool);
        return pool;
    }

    friend struct RenderPlugin;
};
}  // namespace epix::render
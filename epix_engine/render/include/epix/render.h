#pragma once

#include <epix/app.h>
#include <epix/vulkan.h>
#include <epix/render/window.h>

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
    vk::CommandPool get() const {
        std::shared_lock lock(mutex);
        auto thread_id = std::this_thread::get_id();
        if (pools.contains(thread_id)) {
            return pools.at(thread_id);
        }
        lock.unlock();  // unlock before creating a new pool
        auto create_info =
            vk::CommandPoolCreateInfo()
                .setQueueFamilyIndex(queue_family_index)
                .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        vk::CommandPool pool = device.createCommandPool(create_info);
        std::unique_lock unique_lock(mutex);
        pools.emplace(thread_id, pool);
        return pool;
    }

    friend struct RenderPlugin;
};
struct RenderPlugin : public epix::Plugin {
    EPIX_API void build(epix::App&) override;
};
}  // namespace epix::render
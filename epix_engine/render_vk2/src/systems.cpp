#include "epix/rdvk.h"

using namespace epix::render::vulkan2;

EPIX_API void systems::create_context(
    Command cmd,
    Query<Get<Window>, With<PrimaryWindow>> query,
    Res<RenderVKPlugin> plugin
) {
    if (!query) {
        return;
    }
    ZoneScopedN("Create vulkan context");
    auto [window]     = query.single();
    Instance instance = Instance::create(
        "Pixel Engine", VK_MAKE_VERSION(0, 1, 0),
        spdlog::default_logger()->clone("vulkan"), plugin->debug_callback
    );
    PhysicalDevice physical_device =
        instance.instance.enumeratePhysicalDevices().front();
    Device device        = Device::create(instance, physical_device);
    Surface surface      = Surface::create(instance, window.get_handle());
    Swapchain swap_chain = Swapchain::create(device, surface, window.m_vsync);
    CommandPool command_pool = device.createCommandPool(
        vk::CommandPoolCreateInfo()
            .setQueueFamilyIndex(device.queue_family_index)
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
    );
    Queue queue = device.getQueue(device.queue_family_index, 0);
    cmd.spawn(
        instance, physical_device, device, surface, swap_chain, queue,
        command_pool, RenderContext{}
    );
    CommandBuffer cmd_buffer = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    Fence fence = device.createFence(
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    cmd.spawn(cmd_buffer, fence, ContextCommandBuffer{});
}

EPIX_API void systems::destroy_context(
    Command cmd,
    Query<
        Get<Instance, Device, Surface, Swapchain, CommandPool>,
        With<RenderContext>> query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
) {
    if (!query) return;
    if (!cmd_query) return;
    auto [cmd_buffer, cmd_fence] = cmd_query.single();
    auto [instance, device, surface, swap_chain, command_pool] = query.single();
    ZoneScopedN("Destroy vulkan context");
    device.waitForFences(swap_chain.fence(), VK_TRUE, UINT64_MAX);
    device.waitForFences(cmd_fence, VK_TRUE, UINT64_MAX);
    device.destroyFence(cmd_fence);
    device.freeCommandBuffers(command_pool, cmd_buffer);
    swap_chain.destroy();
    device.destroyCommandPool(command_pool);
    surface.destroy();
    device.destroy();
    instance.destroy();
}

EPIX_API void systems::extract_context(
    Extract<
        Get<Instance, Device, Surface, Swapchain, CommandPool, Queue>,
        With<RenderContext>> query,
    Extract<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query,
    Command cmd
) {
    if (!query) return;
    if (!cmd_query) return;
    auto [instance, device, surface, swap_chain, command_pool, queue] =
        query.single();
    auto [cmd_buffer, cmd_fence] = cmd_query.single();
    ZoneScopedN("Extract vulkan context");
    cmd.spawn(
        instance, device, surface, swap_chain, command_pool, queue,
        RenderContext{}
    );
    cmd.spawn(cmd_buffer, cmd_fence, ContextCommandBuffer{});
}

EPIX_API void systems::clear_extracted_context(
    Query<Get<Entity>, With<RenderContext>> query,
    Query<Get<Entity>, With<ContextCommandBuffer>> cmd_query,
    Command cmd
) {
    if (!query) return;
    if (!cmd_query) return;
    ZoneScopedN("Clear extracted vulkan context");
    for (auto [entity] : query.iter()) {
        cmd.entity(entity).despawn();
    }
    for (auto [entity] : cmd_query.iter()) {
        cmd.entity(entity).despawn();
    }
}

EPIX_API void systems::recreate_swap_chain(
    Query<Get<Swapchain>, With<RenderContext>> query
) {
    if (!query) {
        return;
    }
    auto [swap_chain] = query.single();
    ZoneScopedN("Recreate swap chain");
    swap_chain.recreate();
}

EPIX_API void systems::get_next_image(
    Query<Get<Device, Swapchain, CommandPool, Queue>, With<RenderContext>>
        query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
) {
    if (!query) return;
    if (!cmd_query) return;
    auto [cmd_buffer, cmd_fence]                   = cmd_query.single();
    auto [device, swap_chain, command_pool, queue] = query.single();
    ZoneScopedN("Vulkan get next image");
    auto image = swap_chain.next_image();
    device.waitForFences(cmd_fence, VK_TRUE, UINT64_MAX);
    device.resetFences(cmd_fence);
    auto res = device.waitForFences(swap_chain.fence(), VK_TRUE, UINT64_MAX);
    if (res != vk::Result::eSuccess) {
        spdlog::error("Failed to wait for fence: {}", vk::to_string(res));
    }
    cmd_buffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    cmd_buffer.begin(vk::CommandBufferBeginInfo{});
    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::ePresentSrcKHR);
    barrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setImage(swap_chain.current_image());
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1)
    );
    cmd_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, {barrier}
    );
    cmd_buffer.clearColorImage(
        swap_chain.current_image(), vk::ImageLayout::eTransferDstOptimal,
        vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}),
        vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1)
    );
    barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
    barrier.setNewLayout(vk::ImageLayout::eColorAttachmentOptimal);
    barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
    barrier.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
    cmd_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, {barrier}
    );
    cmd_buffer.end();
    auto submit_info = vk::SubmitInfo().setCommandBuffers(cmd_buffer);
    queue.submit(submit_info, cmd_fence);
}

EPIX_API void systems::present_frame(
    Query<Get<Swapchain, Queue, Device, CommandPool>, With<RenderContext>>
        query,
    Query<Get<CommandBuffer, Fence>, With<ContextCommandBuffer>> cmd_query
) {
    if (!query) return;
    if (!cmd_query) return;
    auto [cmd_buffer, cmd_fence]                   = cmd_query.single();
    auto [swap_chain, queue, device, command_pool] = query.single();
    ZoneScopedN("Vulkan present frame");
    device.waitForFences(cmd_fence, VK_TRUE, UINT64_MAX);
    device.resetFences(cmd_fence);
    cmd_buffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    cmd_buffer.begin(vk::CommandBufferBeginInfo{});
    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::eColorAttachmentOptimal);
    barrier.setNewLayout(vk::ImageLayout::ePresentSrcKHR);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setImage(swap_chain.current_image());
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1)
    );
    cmd_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, {barrier}
    );
    cmd_buffer.end();
    auto submit_info = vk::SubmitInfo().setCommandBuffers(cmd_buffer);
    queue.submit(submit_info, cmd_fence);
    try {
        auto present_info =
            vk::PresentInfoKHR()
                .setWaitSemaphores({})
                .setSwapchains(swap_chain.others->swapchain)
                .setImageIndices(swap_chain.others->image_index);
        auto result = queue.presentKHR(present_info);
    } catch (vk::OutOfDateKHRError& e) {
    } catch (std::exception& e) {}
}

#include "epix/rdvk_res.h"

EPIX_API void systems::create_res_manager(
    Command cmd, Query<Get<Device>, With<RenderContext>> query
) {
    if (!query) {
        return;
    }
    ZoneScopedN("Create resource manager");
    auto [device] = query.single();
    ResourceManager res_manager{device};
    cmd.spawn(res_manager, RenderContextResManager{});
}

EPIX_API void systems::destroy_res_manager(
    Command cmd,
    Query<Get<vulkan2::ResourceManager>, With<RenderContextResManager>> query
) {
    if (!query) {
        return;
    }
    ZoneScopedN("Destroy resource manager");
    auto [res_manager] = query.single();
    res_manager.destroy();
}

EPIX_API void systems::extract_res_manager(
    Extract<Get<vulkan2::ResourceManager>, With<RenderContextResManager>> query,
    Query<Get<vulkan2::ResourceManager>, With<RenderContextResManager>>
        render_query,
    Command cmd
) {
    if (!query) {
        return;
    }
    ZoneScopedN("Extract resource manager");
    auto [res_manager] = query.single();
    if (!render_query) {
        cmd.spawn(std::move(res_manager), RenderContextResManager{});
    } else {
        auto [render_res_manager] = render_query.single();
        render_res_manager        = std::move(res_manager);
    }
}

EPIX_API void systems::feedback_res_manager(
    Extract<Get<vulkan2::ResourceManager>, With<RenderContextResManager>> query,
    Query<Get<vulkan2::ResourceManager>, With<RenderContextResManager>>
        main_query
) {
    if (!query || !main_query) {
        return;
    }
    ZoneScopedN("Clear extracted resource manager");
    auto [res_manager]      = query.single();
    auto [main_res_manager] = main_query.single();
    main_res_manager        = std::move(res_manager);
}
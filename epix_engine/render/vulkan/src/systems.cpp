#include "epix/rdvk.h"

using namespace epix::render::vulkan2;
EPIX_API void systems::fn_create_context(
    Command cmd,
    Query<Get<Window>, With<PrimaryWindow>> query,
    Res<VulkanPlugin> plugin
) {
    if (!query) {
        return;
    }
    volkInitialize();
    vk::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);
    ZoneScopedN("Create vulkan context");
    auto [window]     = query.single();
    Instance instance = Instance::create(
        "Pixel Engine", VK_MAKE_VERSION(0, 1, 0),
        spdlog::default_logger()->clone("vulkan"), plugin->debug_callback
    );
    volkLoadInstance(instance.instance);
    vk::defaultDispatchLoaderDynamic.init(instance.instance);
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
    RenderContext context{instance,     physical_device, device,    queue,
                          command_pool, surface,         swap_chain};
    CommandBuffer cmd_buffer = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    Fence fence = device.createFence(
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    CtxCmdBuffer ctx_cmd{cmd_buffer, fence};
    cmd.insert_resource(context);
    cmd.insert_resource(ctx_cmd);
}

EPIX_API void systems::fn_destroy_context(
    Command cmd, ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
) {
    if (!context) return;
    if (!ctx_cmd) return;
    auto& cmd_fence    = ctx_cmd->fence;
    auto& cmd_buffer   = ctx_cmd->cmd_buffer;
    auto& instance     = context->instance;
    auto& device       = context->device;
    auto& surface      = context->primary_surface;
    auto& swap_chain   = context->primary_swapchain;
    auto& command_pool = context->command_pool;
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

EPIX_API void systems::fn_extract_context(
    Extract<ResMut<RenderContext>> context,
    Extract<ResMut<CtxCmdBuffer>> ctx_cmd,
    Command cmd
) {
    if (!context) return;
    if (!ctx_cmd) return;
    ZoneScopedN("Extract vulkan context");
    cmd.share_resource(context);
    cmd.share_resource(ctx_cmd);
}

EPIX_API void systems::fn_clear_extracted_context(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd, Command cmd
) {
    cmd.remove_resource<RenderContext>();
    cmd.remove_resource<CtxCmdBuffer>();
}

EPIX_API void systems::fn_recreate_swap_chain(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
) {
    if (!context || !ctx_cmd) return;
    auto& swap_chain = context->primary_swapchain;
    ZoneScopedN("Recreate swap chain");
    swap_chain.recreate();
    if (swap_chain.need_transition) {
        auto& cmd_buffer = ctx_cmd->cmd_buffer;
        auto& cmd_fence  = ctx_cmd->fence;
        swap_chain.transition_image_layout(cmd_buffer, cmd_fence);
    }
}

EPIX_API void systems::fn_get_next_image(
    ResMut<RenderContext> context,
    ResMut<CtxCmdBuffer> ctx_cmd,
    ResMut<VulkanResources> p_res_manager
) {
    if (!context || !ctx_cmd || !p_res_manager) return;
    auto& swap_chain  = context->primary_swapchain;
    auto& cmd_buffer  = ctx_cmd->cmd_buffer;
    auto& cmd_fence   = ctx_cmd->fence;
    auto& queue       = context->queue;
    auto& device      = context->device;
    auto& res_manager = *p_res_manager;
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
    res_manager.replace_image_view(
        "primary_swapchain", swap_chain.current_image_view()
    );
}

EPIX_API void systems::fn_present_frame(
    ResMut<RenderContext> context, ResMut<CtxCmdBuffer> ctx_cmd
) {
    if (!context || !ctx_cmd) return;
    auto& swap_chain = context->primary_swapchain;
    auto& cmd_buffer = ctx_cmd->cmd_buffer;
    auto& cmd_fence  = ctx_cmd->fence;
    auto& queue      = context->queue;
    auto& device     = context->device;
    ZoneScopedN("Vulkan present frame");
    device.waitForFences(cmd_fence, VK_TRUE, UINT64_MAX);
    device.resetFences(cmd_fence);
    // queue.waitIdle();
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

EPIX_API void systems::fn_create_res_manager(
    Command cmd, Res<RenderContext> context
) {
    if (!context) return;
    ZoneScopedN("Create resource manager");
    auto& device = context->device;
    VulkanResources res_manager{device};
    res_manager.add_image_view(
        "primary_swapchain", context->primary_swapchain.current_image_view()
    );
    cmd.insert_resource(std::move(res_manager));
}

EPIX_API void systems::fn_destroy_res_manager(
    Command cmd, ResMut<VulkanResources> res_manager
) {
    if (!res_manager) return;
    ZoneScopedN("Destroy resource manager");
    res_manager->replace_image_view("primary_swapchain", ImageView());
    res_manager->destroy();
}

EPIX_API void systems::fn_extract_res_manager(
    Extract<ResMut<VulkanResources>> res_manager, Command cmd
) {
    if (!res_manager) return;
    {
        ZoneScopedN("apply resource manager cache");
        res_manager->apply_cache();
    }
    ZoneScopedN("Extract resource manager");
    cmd.share_resource(res_manager);
}

EPIX_API void systems::fn_clear_extracted(
    ResMut<VulkanResources> res_manager, Command cmd
) {
    if (!res_manager) return;
    cmd.remove_resource<VulkanResources>();
}
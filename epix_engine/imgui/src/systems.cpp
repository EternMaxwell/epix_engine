#include "epix/imgui.h"

using namespace epix::prelude;
using namespace epix::render_vk::components;
using namespace epix::window::components;
using namespace epix::imgui;

namespace epix::imgui {
EPIX_API void systems::insert_imgui_ctx(Command cmd) {
    cmd.insert_resource(ImGuiContext{});
}
EPIX_API void systems::init_imgui(
    Query<
        Get<Instance, PhysicalDevice, Device, Queue, CommandPool>,
        With<RenderContext>> context_query,
    Query<Get<Window>, With<PrimaryWindow>> window_query,
    ResMut<ImGuiContext> imgui_context
) {
    if (!context_query) return;
    if (!window_query) return;
    auto [instance, physical_device, device, queue, command_pool] =
        context_query.single();
    auto [window] = window_query.single();
    ZoneScopedN("Init ImGui");
    vk::RenderPassCreateInfo render_pass_info;
    vk::AttachmentDescription color_attachment;
    color_attachment.setFormat(vk::Format::eB8G8R8A8Srgb);
    color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
    color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
    color_attachment.setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal);
    color_attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.setAttachment(0);
    color_attachment_ref.setLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass;
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass.setColorAttachments(color_attachment_ref);
    render_pass_info.setAttachments(color_attachment);
    render_pass_info.setSubpasses(subpass);
    imgui_context->render_pass = RenderPass::create(device, render_pass_info);
    vk::DescriptorPoolSize pool_size;
    pool_size.setType(vk::DescriptorType::eCombinedImageSampler);
    pool_size.setDescriptorCount(1);
    vk::DescriptorPoolCreateInfo pool_info;
    pool_info.setPoolSizes(pool_size);
    pool_info.setMaxSets(1);
    pool_info.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    imgui_context->descriptor_pool = DescriptorPool::create(device, pool_info);
    ImGui::CreateContext();
    imgui_context->context = ImGui::GetCurrentContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window.get_handle(), true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance                  = *instance;
    init_info.PhysicalDevice            = *physical_device;
    init_info.Device                    = *device;
    init_info.Queue                     = *queue;
    init_info.QueueFamily               = device.queue_family_index;
    init_info.PipelineCache             = VK_NULL_HANDLE;
    init_info.DescriptorPool            = imgui_context->descriptor_pool;
    init_info.RenderPass                = imgui_context->render_pass;
    init_info.Allocator                 = nullptr;
    init_info.MinImageCount             = 2;
    init_info.ImageCount                = 2;
    init_info.MSAASamples               = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator                 = nullptr;
    init_info.CheckVkResultFn           = nullptr;
    ImGui_ImplVulkan_Init(&init_info);
    imgui_context->command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);
    imgui_context->fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
}
EPIX_API void systems::deinit_imgui(
    Query<Get<Device, CommandPool>, With<RenderContext>> context_query,
    ResMut<ImGuiContext> imgui_context
) {
    if (!context_query) return;
    auto [device, command_pool] = context_query.single();
    ZoneScopedN("Deinit ImGui");
    device->waitForFences(*imgui_context->fence, VK_TRUE, UINT64_MAX);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    imgui_context->render_pass.destroy(device);
    imgui_context->descriptor_pool.destroy(device);
    imgui_context->command_buffer.free(device, command_pool);
    imgui_context->fence.destroy(device);
    if (imgui_context->framebuffer) imgui_context->framebuffer.destroy(device);
}
EPIX_API void systems::begin_imgui(
    ResMut<ImGuiContext> ctx,
    Query<Get<Device, Queue, Swapchain>, With<RenderContext>> query
) {
    if (!query) return;
    auto [device, queue, swapchain] = query.single();
    ZoneScopedN("Begin ImGui");
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
EPIX_API void systems::end_imgui(
    ResMut<ImGuiContext> ctx,
    Query<Get<Device, Queue, Swapchain>, With<RenderContext>> query
) {
    if (!query) return;
    auto [device, queue, swapchain] = query.single();
    ZoneScopedN("End ImGui");
    ImGui::Render();
    device->waitForFences(*ctx->fence, VK_TRUE, UINT64_MAX);
    device->resetFences(*ctx->fence);
    if (ctx->framebuffer) ctx->framebuffer.destroy(device);
    ctx->command_buffer->reset(vk::CommandBufferResetFlagBits::eReleaseResources
    );
    ctx->command_buffer->begin(vk::CommandBufferBeginInfo{});
    Framebuffer framebuffer = Framebuffer::create(
        device, vk::FramebufferCreateInfo()
                    .setRenderPass(ctx->render_pass)
                    .setAttachments(*swapchain.current_image_view())
                    .setWidth(swapchain.extent.width)
                    .setHeight(swapchain.extent.height)
                    .setLayers(1)
    );
    ctx->framebuffer = framebuffer;
    vk::RenderPassBeginInfo render_pass_info;
    render_pass_info.setRenderPass(ctx->render_pass);
    render_pass_info.setFramebuffer(*framebuffer);
    render_pass_info.setRenderArea(
        vk::Rect2D().setOffset(vk::Offset2D(0, 0)).setExtent(swapchain.extent)
    );
    std::array<vk::ClearValue, 1> clear_values;
    clear_values[0].setColor(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    render_pass_info.setClearValues(clear_values);
    ctx->command_buffer->beginRenderPass(
        render_pass_info, vk::SubpassContents::eInline
    );
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx->command_buffer);
    ctx->command_buffer->endRenderPass();
    ctx->command_buffer->end();
    auto submit_info = vk::SubmitInfo().setCommandBuffers(*ctx->command_buffer);
    queue->submit(submit_info, *ctx->fence);
}
}  // namespace epix::imgui
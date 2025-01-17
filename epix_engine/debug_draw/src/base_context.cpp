#include "epix/render/debug.h"

using namespace epix::render::debug;

EPIX_API vulkan2::PipelineBase::mesh
vulkan2::PipelineBase::Context::generate_mesh() {
    return mesh(max_vertex_count, max_model_count);
}

EPIX_API vulkan2::PipelineBase::Context::Context(
    Device device,
    RenderPass render_pass,
    Pipeline pipeline,
    PipelineLayout pipeline_layout,
    DescriptorSet descriptor_set,
    Buffer vertex_buffer,
    Buffer model_buffer,
    CommandBuffer command_buffer,
    Fence fence,
    size_t max_vertex_count,
    size_t max_model_count
)
    : device(device),
      max_vertex_count(max_vertex_count),
      max_model_count(max_model_count),
      render_pass(render_pass),
      pipeline(pipeline),
      pipeline_layout(pipeline_layout),
      descriptor_set(descriptor_set),
      vertex_buffer(vertex_buffer),
      model_buffer(model_buffer),
      command_buffer(command_buffer),
      fence(fence) {}

EPIX_API void vulkan2::PipelineBase::Context::begin(
    Buffer uniform_buffer, ImageView render_target, vk::Extent2D extent
) {
    auto res = device.waitForFences(fence, VK_TRUE, UINT64_MAX);
    device.resetFences(fence);
    if (framebuffer) {
        device.destroyFramebuffer(framebuffer);
    }
    framebuffer = device.createFramebuffer(vk::FramebufferCreateInfo()
                                               .setRenderPass(render_pass)
                                               .setAttachments(render_target)
                                               .setWidth(extent.width)
                                               .setHeight(extent.height)
                                               .setLayers(1));
    vk::DescriptorBufferInfo buffer_info;
    buffer_info.setBuffer(uniform_buffer);
    buffer_info.setOffset(0);
    buffer_info.setRange(sizeof(glm::mat4) * 2);
    vk::WriteDescriptorSet descriptor_write;
    descriptor_write.setDstSet(descriptor_set);
    descriptor_write.setDstBinding(0);
    descriptor_write.setDstArrayElement(0);
    descriptor_write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    descriptor_write.setDescriptorCount(1);
    descriptor_write.setPBufferInfo(&buffer_info);
    device.updateDescriptorSets({descriptor_write}, {});
    command_buffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    command_buffer.begin(vk::CommandBufferBeginInfo());
    this->extent = extent;
}
EPIX_API void vulkan2::PipelineBase::Context::draw_mesh(mesh& mesh) {
    static auto staging_buffer_resize = [](Device& device, Buffer& buffer,
                                           size_t size) {
        if (buffer) {
            buffer.destroy();
        }
        buffer = device.createBuffer(
            vk::BufferCreateInfo().setSize(size).setUsage(
                vk::BufferUsageFlagBits::eTransferSrc
            ),
            AllocationCreateInfo()
                .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                )
        );
    };
    if (mesh.vertices.empty() || mesh.draw_calls.empty() ||
        mesh.models.empty()) {
        return;
    }
    if (mesh.model_staging_capacity < mesh.models.size()) {
        mesh.model_staging_capacity = mesh.models.size();
        staging_buffer_resize(
            device, mesh.model_staging_buffer,
            sizeof(glm::mat4) * mesh.model_staging_capacity
        );
    }
    if (mesh.vertex_staging_capacity < mesh.vertices.size()) {
        mesh.vertex_staging_capacity = mesh.vertices.size();
        staging_buffer_resize(
            device, mesh.vertex_staging_buffer,
            sizeof(DebugVertex) * mesh.vertex_staging_capacity
        );
    }
    auto* model_data = (glm::mat4*)mesh.model_staging_buffer.map();
    std::memcpy(
        model_data, mesh.models.data(), sizeof(glm::mat4) * mesh.models.size()
    );
    mesh.model_staging_buffer.unmap();
    auto* vertex_data = (DebugVertex*)mesh.vertex_staging_buffer.map();
    std::memcpy(
        vertex_data, mesh.vertices.data(),
        sizeof(DebugVertex) * mesh.vertices.size()
    );
    mesh.vertex_staging_buffer.unmap();
    for (auto& draw_call : mesh.draw_calls) {
        vk::BufferCopy copy_region;
        copy_region.setSize(sizeof(DebugVertex) * draw_call.vertex_count);
        copy_region.setSrcOffset(sizeof(DebugVertex) * draw_call.vertex_offset);
        copy_region.setDstOffset(0);
        command_buffer.copyBuffer(
            mesh.vertex_staging_buffer, vertex_buffer, copy_region
        );
        vk::BufferMemoryBarrier barrier;
        barrier.setBuffer(vertex_buffer);
        barrier.setSize(sizeof(DebugVertex) * draw_call.vertex_count);
        barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        barrier.setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead);
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eVertexInput, {}, {}, barrier, {}
        );
        vk::BufferCopy model_copy_region;
        model_copy_region.setSize(sizeof(glm::mat4) * draw_call.model_count);
        model_copy_region.setSrcOffset(
            sizeof(glm::mat4) * draw_call.model_offset
        );
        model_copy_region.setDstOffset(0);
        command_buffer.copyBuffer(
            mesh.model_staging_buffer, model_buffer, model_copy_region
        );
        vk::BufferMemoryBarrier model_barrier;
        model_barrier.setBuffer(model_buffer);
        model_barrier.setSize(sizeof(glm::mat4) * draw_call.model_count);
        model_barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        model_barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eVertexShader, {}, {}, model_barrier, {}
        );
        begin_pass();
        vk::DeviceSize offsets[] = {0};
        command_buffer.bindVertexBuffers(0, vertex_buffer.buffer, offsets);
        command_buffer.draw(draw_call.vertex_count, 1, 0, 0);
        command_buffer.endRenderPass();
        barrier.setSrcAccessMask(vk::AccessFlagBits::eVertexAttributeRead);
        barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eVertexInput,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, barrier, {}
        );
        model_barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderRead);
        model_barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eVertexShader,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, model_barrier, {}
        );
    }
}
EPIX_API void vulkan2::PipelineBase::Context::end(Queue& queue) {
    command_buffer.end();
    queue.submit(vk::SubmitInfo().setCommandBuffers(command_buffer), fence);
}
EPIX_API void vulkan2::PipelineBase::Context::begin_pass() {
    vk::RenderPassBeginInfo render_pass_info;
    render_pass_info.setRenderPass(render_pass);
    render_pass_info.setFramebuffer(framebuffer);
    render_pass_info.setRenderArea(vk::Rect2D().setExtent(extent));
    command_buffer.beginRenderPass(
        render_pass_info, vk::SubpassContents::eInline
    );
    vk::Viewport viewport;
    viewport.setWidth(extent.width);
    viewport.setHeight(extent.height);
    command_buffer.setViewport(0, viewport);
    vk::Rect2D scissor;
    scissor.setExtent(extent);
    command_buffer.setScissor(0, scissor);
    command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptor_set, {}
    );
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
}
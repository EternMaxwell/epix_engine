#include "epix/render/debug.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::render::debug;

EPIX_API vulkan2::PipelineBase::Context vulkan2::PipelineBase::create_context(
    CommandPool& command_pool, size_t max_vertex_count, size_t max_model_count
) {
    using namespace epix::render::vulkan2;

    auto descriptor_set =
        device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo()
                                          .setDescriptorPool(descriptor_pool)
                                          .setSetLayouts(descriptor_set_layout)
        )[0];
    auto vertex_buffer = device.createBuffer(
        vk::BufferCreateInfo()
            .setSize(sizeof(DebugVertex) * max_vertex_count)
            .setUsage(
                vk::BufferUsageFlagBits::eVertexBuffer |
                vk::BufferUsageFlagBits::eTransferDst
            )
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
    auto model_buffer = device.createBuffer(
        vk::BufferCreateInfo()
            .setSize(sizeof(glm::mat4) * max_model_count)
            .setUsage(
                vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eTransferDst
            )
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
    auto command_buffer = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    auto fence = device.createFence(
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );

    return Context(
        device, render_pass, pipeline, pipeline_layout, descriptor_set,
        vertex_buffer, model_buffer, command_buffer, fence, max_vertex_count,
        max_model_count
    );
}

EPIX_API void vulkan2::PipelineBase::destroy_context(
    Context& context, CommandPool& command_pool
) {
    device.waitForFences(context.fence, VK_TRUE, UINT64_MAX);
    device.destroyFence(context.fence);
    device.destroyBuffer(context.vertex_buffer);
    device.destroyBuffer(context.model_buffer);
    device.freeCommandBuffers(command_pool, context.command_buffer);
    device.freeDescriptorSets(descriptor_pool, context.descriptor_set);
    if (context.framebuffer) {
        device.destroyFramebuffer(context.framebuffer);
    }
}

EPIX_API void vulkan2::PipelineBase::create_descriptor_set_layout() {
    auto bindings = std::array{
        vk::DescriptorSetLayoutBinding()
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        vk::DescriptorSetLayoutBinding()
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex)
    };
    descriptor_set_layout = device.createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo().setBindings(bindings)
    );
}
EPIX_API void vulkan2::PipelineBase::create_pipeline_layout() {
    pipeline_layout = device.createPipelineLayout(
        vk::PipelineLayoutCreateInfo().setSetLayouts(descriptor_set_layout)
    );
}
EPIX_API void vulkan2::PipelineBase::create_descriptor_pool() {
    auto pool_sizes = std::array{
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(256),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(256)
    };
    descriptor_pool = device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo()
            .setMaxSets(256)
            .setPoolSizes(pool_sizes)
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
    );
}
EPIX_API void vulkan2::PipelineBase::create_render_pass() {
    auto attachments = std::array{
        vk::AttachmentDescription()
            .setFormat(vk::Format::eR8G8B8A8Srgb)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eLoad)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
    };
    auto color_attachment =
        vk::AttachmentReference().setAttachment(0).setLayout(
            vk::ImageLayout::eColorAttachmentOptimal
        );
    auto subpass = vk::SubpassDescription()
                       .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                       .setColorAttachments(color_attachment);
    auto dependency =
        vk::SubpassDependency()
            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
            .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
            .setSrcAccessMask({})
            .setDstAccessMask(
                vk::AccessFlagBits::eColorAttachmentRead |
                vk::AccessFlagBits::eColorAttachmentWrite
            );
    render_pass = device.createRenderPass(vk::RenderPassCreateInfo()
                                              .setAttachments(attachments)
                                              .setSubpasses(subpass)
                                              .setDependencies(dependency));
}

EPIX_API void vulkan2::PipelineBase::create_pipeline(
    vk::PrimitiveTopology topology
) {
    using namespace epix::render::vulkan2::util;
    auto& vertex_spv   = debug_vk_vertex_spv;
    auto& fragment_spv = debug_vk_fragment_spv;
    auto vert_source   = std::vector<uint32_t>(
        vertex_spv, vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
    );
    auto vert_module = device.createShaderModule(
        vk::ShaderModuleCreateInfo().setCode(vert_source)
    );
    auto frag_source = std::vector<uint32_t>(
        fragment_spv, fragment_spv + sizeof(fragment_spv) / sizeof(uint32_t)
    );
    auto frag_module = device.createShaderModule(
        vk::ShaderModuleCreateInfo().setCode(frag_source)
    );
    spirv_cross::CompilerGLSL vert(vert_source);
    spirv_cross::CompilerGLSL frag(frag_source);

    auto pipeline_info = vk::GraphicsPipelineCreateInfo();

    auto vertex_input_info = vk::PipelineVertexInputStateCreateInfo();
    auto attributes        = get_vertex_input_attributes(vert);
    auto bindings          = std::vector<vk::VertexInputBindingDescription>{
        vk::VertexInputBindingDescription().setStride(sizeof(DebugVertex))
    };
    default_vertex_input(&vertex_input_info, bindings, attributes);
    pipeline_info.setPVertexInputState(&vertex_input_info);

    auto stages = default_shader_stages(
        vk::ShaderStageFlagBits::eVertex, vert_module,
        vk::ShaderStageFlagBits::eFragment, frag_module
    );
    pipeline_info.setStages(stages);
    pipeline_info.setLayout(pipeline_layout);
    vk::PipelineViewportStateCreateInfo viewport_state;
    auto view_scissors =
        default_viewport_scissor(&viewport_state, {800, 600}, 1);
    pipeline_info.setPViewportState(&viewport_state);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.setTopology(topology);
    pipeline_info.setPInputAssemblyState(&input_assembly);
    vk::PipelineRasterizationStateCreateInfo rasterization;
    default_rasterization(&rasterization);
    pipeline_info.setPRasterizationState(&rasterization);
    vk::PipelineMultisampleStateCreateInfo multisample;
    default_multisample(&multisample);
    pipeline_info.setPMultisampleState(&multisample);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil;
    default_depth_stencil(&depth_stencil);
    pipeline_info.setPDepthStencilState(&depth_stencil);
    auto color_blend_attachments = default_blend_attachments(1);
    vk::PipelineColorBlendStateCreateInfo color_blend;
    color_blend.setAttachments(color_blend_attachments);
    pipeline_info.setPColorBlendState(&color_blend);
    vk::PipelineDynamicStateCreateInfo dynamic_states_info;
    auto dynamic_states = default_dynamic_states(&dynamic_states_info);
    pipeline_info.setPDynamicState(&dynamic_states_info);

    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(0);

    pipeline =
        device.createGraphicsPipeline(vk::PipelineCache(), pipeline_info).value;

    device.destroyShaderModule(vert_module);
    device.destroyShaderModule(frag_module);
}

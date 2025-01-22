#include "epix/render/debug.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

namespace epix::render::debug::vulkan {
using namespace epix::prelude;
using namespace epix::render::debug::vulkan;
using namespace epix::render::debug::vulkan::components;
static std::shared_ptr<spdlog::logger> logger =
    spdlog::default_logger()->clone("draw_dbg");
EPIX_API void systems::create_line_drawer(
    Query<Get<Device, CommandPool>, With<RenderContext>> query,
    Command cmd,
    Res<DebugRenderPlugin> plugin
) {
    if (!query) return;
    auto [device, command_pool] = query.single();
    LineDrawer drawer{
        .max_vertex_count = plugin->max_vertex_count,
        .max_model_count  = plugin->max_model_count
    };
    // RenderPass
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
    drawer.render_pass = RenderPass::create(device, render_pass_info);

    // DescriptorSetLayout
    vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_info;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    vk::DescriptorSetLayoutBinding uniform_binding;
    uniform_binding.setBinding(0);
    uniform_binding.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    uniform_binding.setDescriptorCount(1);
    uniform_binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
    vk::DescriptorSetLayoutBinding model_binding;
    model_binding.setBinding(1);
    model_binding.setDescriptorType(vk::DescriptorType::eStorageBuffer);
    model_binding.setDescriptorCount(1);
    model_binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
    bindings.push_back(uniform_binding);
    bindings.push_back(model_binding);
    descriptor_set_layout_info.setBindings(bindings);
    drawer.descriptor_set_layout =
        DescriptorSetLayout::create(device, descriptor_set_layout_info);

    // PipelineLayout
    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    pipeline_layout_info.setSetLayouts(*drawer.descriptor_set_layout);
    drawer.pipeline_layout =
        PipelineLayout::create(device, pipeline_layout_info);

    // DescriptorPool
    vk::DescriptorPoolSize uniform_pool_size;
    uniform_pool_size.setType(vk::DescriptorType::eUniformBuffer);
    uniform_pool_size.setDescriptorCount(1);
    vk::DescriptorPoolSize model_pool_size;
    model_pool_size.setType(vk::DescriptorType::eStorageBuffer);
    model_pool_size.setDescriptorCount(1);
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    pool_sizes.push_back(uniform_pool_size);
    pool_sizes.push_back(model_pool_size);
    vk::DescriptorPoolCreateInfo descriptor_pool_info;
    descriptor_pool_info.setPoolSizes(pool_sizes);
    descriptor_pool_info.setMaxSets(1);
    descriptor_pool_info.setFlags(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet
    );
    drawer.descriptor_pool =
        DescriptorPool::create(device, descriptor_pool_info);

    // Buffers
    vk::BufferCreateInfo vertex_buffer_info;
    vertex_buffer_info.setSize(sizeof(DebugVertex) * plugin->max_vertex_count);
    vertex_buffer_info.setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
    vertex_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo vertex_alloc_info;
    vertex_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    vertex_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.vertex_buffer =
        Buffer::create(device, vertex_buffer_info, vertex_alloc_info);
    vk::BufferCreateInfo uniform_buffer_info;
    uniform_buffer_info.setSize(sizeof(glm::mat4) * 2);
    uniform_buffer_info.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
    uniform_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo uniform_alloc_info;
    uniform_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    uniform_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.uniform_buffer =
        Buffer::create(device, uniform_buffer_info, uniform_alloc_info);
    vk::BufferCreateInfo model_buffer_info;
    model_buffer_info.setSize(sizeof(glm::mat4) * plugin->max_model_count);
    model_buffer_info.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    model_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo model_alloc_info;
    model_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    model_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.model_buffer =
        Buffer::create(device, model_buffer_info, model_alloc_info);

    // DescriptorSet
    vk::DescriptorSetAllocateInfo descriptor_set_info;
    descriptor_set_info.setDescriptorPool(drawer.descriptor_pool);
    descriptor_set_info.setSetLayouts(*drawer.descriptor_set_layout);
    drawer.descriptor_set = DescriptorSet::create(device, descriptor_set_info);

    // bind buffers
    vk::DescriptorBufferInfo uniform_buffer_desc_info;
    uniform_buffer_desc_info.setBuffer(drawer.uniform_buffer);
    uniform_buffer_desc_info.setOffset(0);
    uniform_buffer_desc_info.setRange(sizeof(glm::mat4) * 2);
    vk::DescriptorBufferInfo model_buffer_desc_info;
    model_buffer_desc_info.setBuffer(drawer.model_buffer);
    model_buffer_desc_info.setOffset(0);
    model_buffer_desc_info.setRange(
        sizeof(glm::mat4) * plugin->max_model_count
    );
    std::vector<vk::WriteDescriptorSet> writes;
    vk::WriteDescriptorSet uniform_write;
    uniform_write.setDstSet(drawer.descriptor_set);
    uniform_write.setDstBinding(0);
    uniform_write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    uniform_write.setBufferInfo(uniform_buffer_desc_info);
    vk::WriteDescriptorSet model_write;
    model_write.setDstSet(drawer.descriptor_set);
    model_write.setDstBinding(1);
    model_write.setDescriptorType(vk::DescriptorType::eStorageBuffer);
    model_write.setBufferInfo(model_buffer_desc_info);
    writes.push_back(uniform_write);
    writes.push_back(model_write);
    device->updateDescriptorSets(writes, {});

    // Pipeline
    vk::ShaderModuleCreateInfo vertex_shader_info;
    vertex_shader_info.setCode(debug_vk_vertex_spv);
    vk::ShaderModuleCreateInfo fragment_shader_info;
    fragment_shader_info.setCode(debug_vk_fragment_spv);
    ShaderModule vertex_shader_module =
        ShaderModule::create(device, vertex_shader_info);
    ShaderModule fragment_shader_module =
        ShaderModule::create(device, fragment_shader_info);
    vk::PipelineShaderStageCreateInfo vertex_shader_stage_info;
    vertex_shader_stage_info.setStage(vk::ShaderStageFlagBits::eVertex);
    vertex_shader_stage_info.setModule(vertex_shader_module);
    vertex_shader_stage_info.setPName("main");
    vk::PipelineShaderStageCreateInfo fragment_shader_stage_info;
    fragment_shader_stage_info.setStage(vk::ShaderStageFlagBits::eFragment);
    fragment_shader_stage_info.setModule(fragment_shader_module);
    fragment_shader_stage_info.setPName("main");
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    shader_stages.push_back(vertex_shader_stage_info);
    shader_stages.push_back(fragment_shader_stage_info);
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    std::vector<vk::VertexInputAttributeDescription> attribute_desc = {
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(DebugVertex, pos)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(DebugVertex, color)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(2)
            .setFormat(vk::Format::eR32Uint)
            .setOffset(offsetof(DebugVertex, model_index))
    };
    vk::VertexInputBindingDescription binding_desc;
    binding_desc.setBinding(0);
    binding_desc.setStride(sizeof(DebugVertex));
    binding_desc.setInputRate(vk::VertexInputRate::eVertex);
    vertex_input_info.setVertexBindingDescriptions(binding_desc);
    vertex_input_info.setVertexAttributeDescriptions(attribute_desc);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
    input_assembly_info.setTopology(vk::PrimitiveTopology::eLineList);
    vk::PipelineViewportStateCreateInfo viewport_info;
    viewport_info.setViewportCount(1);
    viewport_info.setScissorCount(1);
    vk::PipelineRasterizationStateCreateInfo rasterization_info;
    rasterization_info.setDepthClampEnable(VK_FALSE);
    rasterization_info.setRasterizerDiscardEnable(VK_FALSE);
    rasterization_info.setPolygonMode(vk::PolygonMode::eFill);
    rasterization_info.setLineWidth(1.0f);
    rasterization_info.setCullMode({});
    vk::PipelineMultisampleStateCreateInfo multisample_info;
    multisample_info.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setBlendEnable(VK_TRUE);
    color_blend_attachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );
    color_blend_attachment.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha);
    color_blend_attachment.setDstColorBlendFactor(
        vk::BlendFactor::eOneMinusSrcAlpha
    );
    color_blend_attachment.setColorBlendOp(vk::BlendOp::eAdd);
    color_blend_attachment.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
    color_blend_attachment.setDstAlphaBlendFactor(vk::BlendFactor::eZero);
    vk::PipelineColorBlendStateCreateInfo color_blend_info;
    color_blend_info.setAttachments(color_blend_attachment);
    color_blend_info.setLogicOpEnable(VK_FALSE);
    color_blend_info.setLogicOp(vk::LogicOp::eCopy);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
    depth_stencil_info.setDepthTestEnable(VK_FALSE);
    depth_stencil_info.setDepthWriteEnable(VK_FALSE);
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state_info;
    dynamic_state_info.setDynamicStates(dynamic_states);
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(shader_stages);
    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly_info);
    pipeline_info.setPViewportState(&viewport_info);
    pipeline_info.setPRasterizationState(&rasterization_info);
    pipeline_info.setPMultisampleState(&multisample_info);
    pipeline_info.setPColorBlendState(&color_blend_info);
    pipeline_info.setPDepthStencilState(&depth_stencil_info);
    pipeline_info.setPDynamicState(&dynamic_state_info);
    pipeline_info.setLayout(drawer.pipeline_layout);
    pipeline_info.setRenderPass(drawer.render_pass);
    pipeline_info.setSubpass(0);
    drawer.pipeline = Pipeline::create(device, pipeline_info);

    drawer.fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    drawer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);

    cmd.spawn(std::move(drawer));
    vertex_shader_module.destroy(device);
    fragment_shader_module.destroy(device);
}

EPIX_API void systems::destroy_line_drawer(
    Query<Get<Device>, With<RenderContext>> device_query,
    Query<Get<LineDrawer>> query
) {
    if (!device_query) return;
    if (!query) return;
    auto [device] = device_query.single();
    auto [drawer] = query.single();
    logger->debug("Destroy line drawer.");
    device->waitForFences(*drawer.fence, VK_TRUE, UINT64_MAX);
    drawer.render_pass.destroy(device);
    drawer.pipeline.destroy(device);
    drawer.pipeline_layout.destroy(device);
    drawer.vertex_buffer.destroy(device);
    drawer.uniform_buffer.destroy(device);
    drawer.model_buffer.destroy(device);
    drawer.descriptor_set_layout.destroy(device);
    drawer.descriptor_set.destroy(device, drawer.descriptor_pool);
    drawer.descriptor_pool.destroy(device);
    drawer.fence.destroy(device);
    if (drawer.framebuffer) drawer.framebuffer.destroy(device);
}
EPIX_API void systems::create_point_drawer(
    Query<Get<Device, CommandPool>, With<RenderContext>> query,
    Command cmd,
    Res<DebugRenderPlugin> plugin
) {
    if (!query) return;
    auto [device, command_pool] = query.single();
    PointDrawer drawer{
        .max_vertex_count = plugin->max_vertex_count,
        .max_model_count  = plugin->max_model_count
    };
    // RenderPass
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
    drawer.render_pass = RenderPass::create(device, render_pass_info);

    // DescriptorSetLayout
    vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_info;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    vk::DescriptorSetLayoutBinding uniform_binding;
    uniform_binding.setBinding(0);
    uniform_binding.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    uniform_binding.setDescriptorCount(1);
    uniform_binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
    vk::DescriptorSetLayoutBinding model_binding;
    model_binding.setBinding(1);
    model_binding.setDescriptorType(vk::DescriptorType::eStorageBuffer);
    model_binding.setDescriptorCount(1);
    model_binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
    bindings.push_back(uniform_binding);
    bindings.push_back(model_binding);
    descriptor_set_layout_info.setBindings(bindings);
    drawer.descriptor_set_layout =
        DescriptorSetLayout::create(device, descriptor_set_layout_info);

    // PipelineLayout
    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    pipeline_layout_info.setSetLayouts(*drawer.descriptor_set_layout);
    drawer.pipeline_layout =
        PipelineLayout::create(device, pipeline_layout_info);

    // DescriptorPool
    vk::DescriptorPoolSize uniform_pool_size;
    uniform_pool_size.setType(vk::DescriptorType::eUniformBuffer);
    uniform_pool_size.setDescriptorCount(1);
    vk::DescriptorPoolSize model_pool_size;
    model_pool_size.setType(vk::DescriptorType::eStorageBuffer);
    model_pool_size.setDescriptorCount(1);
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    pool_sizes.push_back(uniform_pool_size);
    pool_sizes.push_back(model_pool_size);
    vk::DescriptorPoolCreateInfo descriptor_pool_info;
    descriptor_pool_info.setPoolSizes(pool_sizes);
    descriptor_pool_info.setMaxSets(1);
    descriptor_pool_info.setFlags(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet
    );
    drawer.descriptor_pool =
        DescriptorPool::create(device, descriptor_pool_info);

    // Buffers
    vk::BufferCreateInfo vertex_buffer_info;
    vertex_buffer_info.setSize(sizeof(DebugVertex) * plugin->max_vertex_count);
    vertex_buffer_info.setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
    vertex_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo vertex_alloc_info;
    vertex_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    vertex_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.vertex_buffer =
        Buffer::create(device, vertex_buffer_info, vertex_alloc_info);
    vk::BufferCreateInfo uniform_buffer_info;
    uniform_buffer_info.setSize(sizeof(glm::mat4) * 2);
    uniform_buffer_info.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
    uniform_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo uniform_alloc_info;
    uniform_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    uniform_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.uniform_buffer =
        Buffer::create(device, uniform_buffer_info, uniform_alloc_info);
    vk::BufferCreateInfo model_buffer_info;
    model_buffer_info.setSize(sizeof(glm::mat4) * plugin->max_model_count);
    model_buffer_info.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    model_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo model_alloc_info;
    model_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    model_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.model_buffer =
        Buffer::create(device, model_buffer_info, model_alloc_info);

    // DescriptorSet
    vk::DescriptorSetAllocateInfo descriptor_set_info;
    descriptor_set_info.setDescriptorPool(drawer.descriptor_pool);
    descriptor_set_info.setSetLayouts(*drawer.descriptor_set_layout);
    drawer.descriptor_set = DescriptorSet::create(device, descriptor_set_info);

    // bind buffers
    vk::DescriptorBufferInfo uniform_buffer_desc_info;
    uniform_buffer_desc_info.setBuffer(drawer.uniform_buffer);
    uniform_buffer_desc_info.setOffset(0);
    uniform_buffer_desc_info.setRange(sizeof(glm::mat4) * 2);
    vk::DescriptorBufferInfo model_buffer_desc_info;
    model_buffer_desc_info.setBuffer(drawer.model_buffer);
    model_buffer_desc_info.setOffset(0);
    model_buffer_desc_info.setRange(
        sizeof(glm::mat4) * plugin->max_model_count
    );
    std::vector<vk::WriteDescriptorSet> writes;
    vk::WriteDescriptorSet uniform_write;
    uniform_write.setDstSet(drawer.descriptor_set);
    uniform_write.setDstBinding(0);
    uniform_write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    uniform_write.setBufferInfo(uniform_buffer_desc_info);
    vk::WriteDescriptorSet model_write;
    model_write.setDstSet(drawer.descriptor_set);
    model_write.setDstBinding(1);
    model_write.setDescriptorType(vk::DescriptorType::eStorageBuffer);
    model_write.setBufferInfo(model_buffer_desc_info);
    writes.push_back(uniform_write);
    writes.push_back(model_write);
    device->updateDescriptorSets(writes, {});

    // Pipeline
    vk::ShaderModuleCreateInfo vertex_shader_info;
    vertex_shader_info.setCode(debug_vk_vertex_spv);
    vk::ShaderModuleCreateInfo fragment_shader_info;
    fragment_shader_info.setCode(debug_vk_fragment_spv);
    ShaderModule vertex_shader_module =
        ShaderModule::create(device, vertex_shader_info);
    ShaderModule fragment_shader_module =
        ShaderModule::create(device, fragment_shader_info);
    vk::PipelineShaderStageCreateInfo vertex_shader_stage_info;
    vertex_shader_stage_info.setStage(vk::ShaderStageFlagBits::eVertex);
    vertex_shader_stage_info.setModule(vertex_shader_module);
    vertex_shader_stage_info.setPName("main");
    vk::PipelineShaderStageCreateInfo fragment_shader_stage_info;
    fragment_shader_stage_info.setStage(vk::ShaderStageFlagBits::eFragment);
    fragment_shader_stage_info.setModule(fragment_shader_module);
    fragment_shader_stage_info.setPName("main");
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    shader_stages.push_back(vertex_shader_stage_info);
    shader_stages.push_back(fragment_shader_stage_info);
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    std::vector<vk::VertexInputAttributeDescription> attribute_desc = {
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(DebugVertex, pos)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(DebugVertex, color)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(2)
            .setFormat(vk::Format::eR32Uint)
            .setOffset(offsetof(DebugVertex, model_index))
    };
    vk::VertexInputBindingDescription binding_desc;
    binding_desc.setBinding(0);
    binding_desc.setStride(sizeof(DebugVertex));
    binding_desc.setInputRate(vk::VertexInputRate::eVertex);
    vertex_input_info.setVertexBindingDescriptions(binding_desc);
    vertex_input_info.setVertexAttributeDescriptions(attribute_desc);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
    input_assembly_info.setTopology(vk::PrimitiveTopology::ePointList);
    vk::PipelineViewportStateCreateInfo viewport_info;
    viewport_info.setViewportCount(1);
    viewport_info.setScissorCount(1);
    vk::PipelineRasterizationStateCreateInfo rasterization_info;
    rasterization_info.setDepthClampEnable(VK_FALSE);
    rasterization_info.setRasterizerDiscardEnable(VK_FALSE);
    rasterization_info.setPolygonMode(vk::PolygonMode::eFill);
    rasterization_info.setLineWidth(1.0f);
    rasterization_info.setCullMode({});
    vk::PipelineMultisampleStateCreateInfo multisample_info;
    multisample_info.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setBlendEnable(VK_TRUE);
    color_blend_attachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );
    color_blend_attachment.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha);
    color_blend_attachment.setDstColorBlendFactor(
        vk::BlendFactor::eOneMinusSrcAlpha
    );
    color_blend_attachment.setColorBlendOp(vk::BlendOp::eAdd);
    color_blend_attachment.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
    color_blend_attachment.setDstAlphaBlendFactor(vk::BlendFactor::eZero);
    vk::PipelineColorBlendStateCreateInfo color_blend_info;
    color_blend_info.setAttachments(color_blend_attachment);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
    depth_stencil_info.setDepthTestEnable(VK_FALSE);
    depth_stencil_info.setDepthWriteEnable(VK_FALSE);
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state_info;
    dynamic_state_info.setDynamicStates(dynamic_states);
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(shader_stages);
    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly_info);
    pipeline_info.setPViewportState(&viewport_info);
    pipeline_info.setPRasterizationState(&rasterization_info);
    pipeline_info.setPMultisampleState(&multisample_info);
    pipeline_info.setPColorBlendState(&color_blend_info);
    pipeline_info.setPDepthStencilState(&depth_stencil_info);
    pipeline_info.setPDynamicState(&dynamic_state_info);
    pipeline_info.setLayout(drawer.pipeline_layout);
    pipeline_info.setRenderPass(drawer.render_pass);
    pipeline_info.setSubpass(0);
    drawer.pipeline = Pipeline::create(device, pipeline_info);

    drawer.fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    drawer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);

    cmd.spawn(std::move(drawer));
    vertex_shader_module.destroy(device);
    fragment_shader_module.destroy(device);
}

EPIX_API void systems::destroy_point_drawer(
    Query<Get<Device>, With<RenderContext>> device_query,
    Query<Get<PointDrawer>> query
) {
    if (!device_query) return;
    if (!query) return;
    auto [device] = device_query.single();
    auto [drawer] = query.single();
    logger->debug("Destroy point drawer.");
    device->waitForFences(*drawer.fence, VK_TRUE, UINT64_MAX);
    drawer.render_pass.destroy(device);
    drawer.pipeline.destroy(device);
    drawer.pipeline_layout.destroy(device);
    drawer.vertex_buffer.destroy(device);
    drawer.uniform_buffer.destroy(device);
    drawer.model_buffer.destroy(device);
    drawer.descriptor_set_layout.destroy(device);
    drawer.descriptor_set.destroy(device, drawer.descriptor_pool);
    drawer.descriptor_pool.destroy(device);
    drawer.fence.destroy(device);
    if (drawer.framebuffer) drawer.framebuffer.destroy(device);
}

EPIX_API void systems::create_triangle_drawer(
    Query<Get<Device, CommandPool>, With<RenderContext>> query,
    Command cmd,
    Res<DebugRenderPlugin> plugin
) {
    if (!query) return;
    auto [device, command_pool] = query.single();
    TriangleDrawer drawer{
        .max_vertex_count = plugin->max_vertex_count,
        .max_model_count  = plugin->max_model_count
    };
    // RenderPass
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
    drawer.render_pass = RenderPass::create(device, render_pass_info);

    // DescriptorSetLayout
    vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_info;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    vk::DescriptorSetLayoutBinding uniform_binding;
    uniform_binding.setBinding(0);
    uniform_binding.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    uniform_binding.setDescriptorCount(1);
    uniform_binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
    vk::DescriptorSetLayoutBinding model_binding;
    model_binding.setBinding(1);
    model_binding.setDescriptorType(vk::DescriptorType::eStorageBuffer);
    model_binding.setDescriptorCount(1);
    model_binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
    bindings.push_back(uniform_binding);
    bindings.push_back(model_binding);
    descriptor_set_layout_info.setBindings(bindings);
    drawer.descriptor_set_layout =
        DescriptorSetLayout::create(device, descriptor_set_layout_info);

    // PipelineLayout
    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    pipeline_layout_info.setSetLayouts(*drawer.descriptor_set_layout);
    drawer.pipeline_layout =
        PipelineLayout::create(device, pipeline_layout_info);

    // DescriptorPool
    vk::DescriptorPoolSize uniform_pool_size;
    uniform_pool_size.setType(vk::DescriptorType::eUniformBuffer);
    uniform_pool_size.setDescriptorCount(1);
    vk::DescriptorPoolSize model_pool_size;
    model_pool_size.setType(vk::DescriptorType::eStorageBuffer);
    model_pool_size.setDescriptorCount(1);
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    pool_sizes.push_back(uniform_pool_size);
    pool_sizes.push_back(model_pool_size);
    vk::DescriptorPoolCreateInfo descriptor_pool_info;
    descriptor_pool_info.setPoolSizes(pool_sizes);
    descriptor_pool_info.setMaxSets(1);
    descriptor_pool_info.setFlags(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet
    );
    drawer.descriptor_pool =
        DescriptorPool::create(device, descriptor_pool_info);

    // Buffers
    vk::BufferCreateInfo vertex_buffer_info;
    vertex_buffer_info.setSize(sizeof(DebugVertex) * plugin->max_vertex_count);
    vertex_buffer_info.setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
    vertex_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo vertex_alloc_info;
    vertex_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    vertex_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.vertex_buffer =
        Buffer::create(device, vertex_buffer_info, vertex_alloc_info);
    vk::BufferCreateInfo uniform_buffer_info;
    uniform_buffer_info.setSize(sizeof(glm::mat4) * 2);
    uniform_buffer_info.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
    uniform_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo uniform_alloc_info;
    uniform_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    uniform_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.uniform_buffer =
        Buffer::create(device, uniform_buffer_info, uniform_alloc_info);
    vk::BufferCreateInfo model_buffer_info;
    model_buffer_info.setSize(sizeof(glm::mat4) * plugin->max_model_count);
    model_buffer_info.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    model_buffer_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo model_alloc_info;
    model_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    model_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    drawer.model_buffer =
        Buffer::create(device, model_buffer_info, model_alloc_info);

    // DescriptorSet
    vk::DescriptorSetAllocateInfo descriptor_set_info;
    descriptor_set_info.setDescriptorPool(drawer.descriptor_pool);
    descriptor_set_info.setSetLayouts(*drawer.descriptor_set_layout);
    drawer.descriptor_set = DescriptorSet::create(device, descriptor_set_info);

    // bind buffers
    vk::DescriptorBufferInfo uniform_buffer_desc_info;
    uniform_buffer_desc_info.setBuffer(drawer.uniform_buffer);
    uniform_buffer_desc_info.setOffset(0);
    uniform_buffer_desc_info.setRange(sizeof(glm::mat4) * 2);
    vk::DescriptorBufferInfo model_buffer_desc_info;
    model_buffer_desc_info.setBuffer(drawer.model_buffer);
    model_buffer_desc_info.setOffset(0);
    model_buffer_desc_info.setRange(
        sizeof(glm::mat4) * plugin->max_model_count
    );
    std::vector<vk::WriteDescriptorSet> writes;
    vk::WriteDescriptorSet uniform_write;
    uniform_write.setDstSet(drawer.descriptor_set);
    uniform_write.setDstBinding(0);
    uniform_write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    uniform_write.setBufferInfo(uniform_buffer_desc_info);
    vk::WriteDescriptorSet model_write;
    model_write.setDstSet(drawer.descriptor_set);
    model_write.setDstBinding(1);
    model_write.setDescriptorType(vk::DescriptorType::eStorageBuffer);
    model_write.setBufferInfo(model_buffer_desc_info);
    writes.push_back(uniform_write);
    writes.push_back(model_write);
    device->updateDescriptorSets(writes, {});

    // Pipeline
    vk::ShaderModuleCreateInfo vertex_shader_info;
    vertex_shader_info.setCode(debug_vk_vertex_spv);
    vk::ShaderModuleCreateInfo fragment_shader_info;
    fragment_shader_info.setCode(debug_vk_fragment_spv);
    ShaderModule vertex_shader_module =
        ShaderModule::create(device, vertex_shader_info);
    ShaderModule fragment_shader_module =
        ShaderModule::create(device, fragment_shader_info);
    vk::PipelineShaderStageCreateInfo vertex_shader_stage_info;
    vertex_shader_stage_info.setStage(vk::ShaderStageFlagBits::eVertex);
    vertex_shader_stage_info.setModule(vertex_shader_module);
    vertex_shader_stage_info.setPName("main");
    vk::PipelineShaderStageCreateInfo fragment_shader_stage_info;
    fragment_shader_stage_info.setStage(vk::ShaderStageFlagBits::eFragment);
    fragment_shader_stage_info.setModule(fragment_shader_module);
    fragment_shader_stage_info.setPName("main");
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    shader_stages.push_back(vertex_shader_stage_info);
    shader_stages.push_back(fragment_shader_stage_info);
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    std::vector<vk::VertexInputAttributeDescription> attribute_desc = {
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(DebugVertex, pos)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(DebugVertex, color)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(2)
            .setFormat(vk::Format::eR32Uint)
            .setOffset(offsetof(DebugVertex, model_index))
    };
    vk::VertexInputBindingDescription binding_desc;
    binding_desc.setBinding(0);
    binding_desc.setStride(sizeof(DebugVertex));
    binding_desc.setInputRate(vk::VertexInputRate::eVertex);
    vertex_input_info.setVertexBindingDescriptions(binding_desc);
    vertex_input_info.setVertexAttributeDescriptions(attribute_desc);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
    input_assembly_info.setTopology(vk::PrimitiveTopology::eTriangleList);
    vk::PipelineViewportStateCreateInfo viewport_info;
    viewport_info.setViewportCount(1);
    viewport_info.setScissorCount(1);
    vk::PipelineRasterizationStateCreateInfo rasterization_info;
    rasterization_info.setDepthClampEnable(VK_FALSE);
    rasterization_info.setRasterizerDiscardEnable(VK_FALSE);
    rasterization_info.setPolygonMode(vk::PolygonMode::eFill);
    rasterization_info.setLineWidth(1.0f);
    rasterization_info.setCullMode({});
    vk::PipelineMultisampleStateCreateInfo multisample_info;
    multisample_info.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setBlendEnable(VK_TRUE);
    color_blend_attachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );
    color_blend_attachment.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha);
    color_blend_attachment.setDstColorBlendFactor(
        vk::BlendFactor::eOneMinusSrcAlpha
    );
    color_blend_attachment.setColorBlendOp(vk::BlendOp::eAdd);
    color_blend_attachment.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
    color_blend_attachment.setDstAlphaBlendFactor(vk::BlendFactor::eZero);
    vk::PipelineColorBlendStateCreateInfo color_blend_info;
    color_blend_info.setAttachments(color_blend_attachment);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
    depth_stencil_info.setDepthTestEnable(VK_FALSE);
    depth_stencil_info.setDepthWriteEnable(VK_FALSE);
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state_info;
    dynamic_state_info.setDynamicStates(dynamic_states);
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(shader_stages);
    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly_info);
    pipeline_info.setPViewportState(&viewport_info);
    pipeline_info.setPRasterizationState(&rasterization_info);
    pipeline_info.setPMultisampleState(&multisample_info);
    pipeline_info.setPColorBlendState(&color_blend_info);
    pipeline_info.setPDepthStencilState(&depth_stencil_info);
    pipeline_info.setPDynamicState(&dynamic_state_info);
    pipeline_info.setLayout(drawer.pipeline_layout);
    pipeline_info.setRenderPass(drawer.render_pass);
    pipeline_info.setSubpass(0);
    drawer.pipeline = Pipeline::create(device, pipeline_info);

    drawer.fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    drawer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);

    cmd.spawn(std::move(drawer));
    vertex_shader_module.destroy(device);
    fragment_shader_module.destroy(device);
}

EPIX_API void systems::destroy_triangle_drawer(
    Query<Get<Device>, With<RenderContext>> device_query,
    Query<Get<TriangleDrawer>> query
) {
    if (!device_query) return;
    if (!query) return;
    auto [device] = device_query.single();
    auto [drawer] = query.single();
    logger->debug("Destroy triangle drawer.");
    device->waitForFences(*drawer.fence, VK_TRUE, UINT64_MAX);
    drawer.render_pass.destroy(device);
    drawer.pipeline.destroy(device);
    drawer.pipeline_layout.destroy(device);
    drawer.vertex_buffer.destroy(device);
    drawer.uniform_buffer.destroy(device);
    drawer.model_buffer.destroy(device);
    drawer.descriptor_set_layout.destroy(device);
    drawer.descriptor_set.destroy(device, drawer.descriptor_pool);
    drawer.descriptor_pool.destroy(device);
    drawer.fence.destroy(device);
    if (drawer.framebuffer) drawer.framebuffer.destroy(device);
}
}  // namespace epix::render::debug::vulkan

namespace epix::render::debug::vulkan2 {
EPIX_API void systems::create_pipelines(
    Command cmd, Res<render::vulkan2::RenderContext> context
) {
    if (!context) return;
    auto& device       = context->device;
    auto& command_pool = context->command_pool;
    DebugPipelines pipelines(device);
    pipelines.create();
    cmd.insert_resource(std::move(pipelines));
}
EPIX_API void systems::extract_pipelines(
    ResMut<DebugPipelines> pipelines, Command cmd
) {
    if (!pipelines) return;
    cmd.share_resource(pipelines);
}
EPIX_API void systems::destroy_pipelines(
    Command cmd, ResMut<DebugPipelines> pipelines
) {
    if (!pipelines) return;
    pipelines->destroy();
}
}  // namespace epix::render::debug::vulkan2
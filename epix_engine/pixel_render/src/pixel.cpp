#include <epix/font.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tracy/Tracy.hpp>

#include "epix/render/pixel.h"
#include "shaders/block/fragment_shader.h"
#include "shaders/block/geometry_shader.h"
#include "shaders/block/vertex_shader.h"
#include "shaders/pixel/fragment_shader.h"
#include "shaders/pixel/geometry_shader.h"
#include "shaders/pixel/vertex_shader.h"

namespace epix::render::pixel {
EPIX_API void systems::create_pixel_block_pipeline(
    Command command, Query<Get<Device, CommandPool>, With<RenderContext>> query
) {
    if (!query) return;
    auto [device, command_pool] = query.single();

    // BUFFER CREATION
    vk::BufferCreateInfo vertex_buffer_info;
    vertex_buffer_info.setUsage(
        vk::BufferUsageFlagBits::eVertexBuffer |
        vk::BufferUsageFlagBits::eTransferDst
    );
    vertex_buffer_info.setSize(sizeof(PixelBlockVertex) * 4 * 1024 * 1024);
    AllocationCreateInfo vertex_alloc_info;
    vertex_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    vertex_alloc_info.setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
    Buffer vertex_buffer =
        Buffer::create(device, vertex_buffer_info, vertex_alloc_info);
    vk::BufferCreateInfo vertex_staging_buffer_info;
    vertex_staging_buffer_info.setUsage(vk::BufferUsageFlagBits::eTransferSrc);
    vertex_staging_buffer_info.setSize(
        sizeof(PixelBlockVertex) * 4 * 1024 * 1024
    );
    AllocationCreateInfo vertex_staging_alloc_info;
    vertex_staging_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    vertex_staging_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
    Buffer vertex_staging_buffer = Buffer::create(
        device, vertex_staging_buffer_info, vertex_staging_alloc_info
    );
    vk::BufferCreateInfo uniform_buffer_info;
    uniform_buffer_info.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
    uniform_buffer_info.setSize(sizeof(PixelUniformBuffer));
    AllocationCreateInfo uniform_alloc_info;
    uniform_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    uniform_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    Buffer uniform_buffer =
        Buffer::create(device, uniform_buffer_info, uniform_alloc_info);
    vk::BufferCreateInfo block_model_buffer_info;
    block_model_buffer_info.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    block_model_buffer_info.setSize(sizeof(PixelBlockData) * 4 * 1024);
    AllocationCreateInfo block_model_alloc_info;
    block_model_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    block_model_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    Buffer block_model_buffer =
        Buffer::create(device, block_model_buffer_info, block_model_alloc_info);

    // DESCRIPTOR SET LAYOUT
    DescriptorSetLayout descriptor_set_layout = DescriptorSetLayout::create(
        device, {vk::DescriptorSetLayoutBinding()
                     .setBinding(0)
                     .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eGeometry),
                 vk::DescriptorSetLayoutBinding()
                     .setBinding(1)
                     .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eGeometry)}
    );

    // DESCRIPTOR POOL
    auto pool_sizes = std::vector<vk::DescriptorPoolSize>{
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
    };
    DescriptorPool descriptor_pool = DescriptorPool::create(
        device, vk::DescriptorPoolCreateInfo()
                    .setMaxSets(1)
                    .setPoolSizes(pool_sizes)
                    .setFlags(
                        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBindEXT
                    )
    );

    // DESCRIPTOR SET
    DescriptorSet descriptor_set = DescriptorSet::create(
        device, vk::DescriptorSetAllocateInfo()
                    .setDescriptorPool(descriptor_pool)
                    .setSetLayouts(*descriptor_set_layout)
    );

    // PIPELINE LAYOUT
    PipelineLayout pipeline_layout = PipelineLayout::create(
        device,
        vk::PipelineLayoutCreateInfo().setSetLayouts(*descriptor_set_layout)
    );

    // RENDER PASS
    vk::AttachmentDescription color_attachment;
    color_attachment.setFormat(vk::Format::eB8G8R8A8Srgb);
    color_attachment.setSamples(vk::SampleCountFlagBits::e1);
    color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
    color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
    color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
    color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
    color_attachment.setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal);
    color_attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.setAttachment(0);
    color_attachment_ref.setLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass;
    subpass.setColorAttachments(color_attachment_ref);
    vk::RenderPassCreateInfo render_pass_info;
    render_pass_info.setAttachments(color_attachment);
    render_pass_info.setSubpasses(subpass);
    RenderPass render_pass = RenderPass::create(device, render_pass_info);

    // SHADER MODULES
    ShaderModule vert_shader_module = ShaderModule::create(
        device, vk::ShaderModuleCreateInfo().setCode(pixel_block_vk_vertex_spv)
    );
    ShaderModule frag_shader_module = ShaderModule::create(
        device,
        vk::ShaderModuleCreateInfo().setCode(pixel_block_vk_fragment_spv)
    );
    ShaderModule geom_shader_module = ShaderModule::create(
        device,
        vk::ShaderModuleCreateInfo().setCode(pixel_block_vk_geometry_spv)
    );
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = {
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vert_shader_module)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eGeometry)
            .setModule(*geom_shader_module)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*frag_shader_module)
            .setPName("main")
    };

    // GRAPHICS PIPELINE
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    vertex_input_info.setVertexBindingDescriptionCount(1);
    vk::VertexInputBindingDescription binding_description;
    binding_description.setBinding(0);
    binding_description.setStride(sizeof(PixelBlockVertex));
    binding_description.setInputRate(vk::VertexInputRate::eVertex);
    vertex_input_info.setPVertexBindingDescriptions(&binding_description);
    vk::VertexInputAttributeDescription attribute_descriptions[2] = {
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(PixelBlockVertex, color)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(1)
            .setFormat(vk::Format::eR32Sint)
            .setOffset(offsetof(PixelBlockVertex, model_index))
    };
    vertex_input_info.setVertexAttributeDescriptions(attribute_descriptions);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
    input_assembly_info.setTopology(vk::PrimitiveTopology::ePointList);
    input_assembly_info.setPrimitiveRestartEnable(VK_FALSE);
    vk::PipelineViewportStateCreateInfo viewport_state_info;
    viewport_state_info.setViewportCount(1);
    viewport_state_info.setScissorCount(1);
    vk::PipelineRasterizationStateCreateInfo rasterizer_info;
    rasterizer_info.setDepthClampEnable(VK_FALSE);
    rasterizer_info.setRasterizerDiscardEnable(VK_FALSE);
    rasterizer_info.setPolygonMode(vk::PolygonMode::eFill);
    rasterizer_info.setLineWidth(1.0f);
    rasterizer_info.setCullMode({});
    rasterizer_info.setFrontFace(vk::FrontFace::eClockwise);
    rasterizer_info.setDepthBiasEnable(VK_FALSE);
    vk::PipelineMultisampleStateCreateInfo multisampling_info;
    multisampling_info.setSampleShadingEnable(VK_FALSE);
    multisampling_info.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );
    color_blend_attachment.setBlendEnable(VK_TRUE);
    color_blend_attachment.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha);
    color_blend_attachment.setDstColorBlendFactor(
        vk::BlendFactor::eOneMinusSrcAlpha
    );
    color_blend_attachment.setColorBlendOp(vk::BlendOp::eAdd);
    color_blend_attachment.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
    color_blend_attachment.setDstAlphaBlendFactor(vk::BlendFactor::eZero);
    color_blend_attachment.setAlphaBlendOp(vk::BlendOp::eAdd);
    vk::PipelineColorBlendStateCreateInfo color_blending_info;
    color_blending_info.setLogicOpEnable(VK_FALSE);
    color_blending_info.setLogicOp(vk::LogicOp::eCopy);
    color_blending_info.setAttachmentCount(1);
    color_blending_info.setPAttachments(&color_blend_attachment);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
    depth_stencil_info.setDepthTestEnable(VK_TRUE);
    depth_stencil_info.setDepthWriteEnable(VK_TRUE);
    depth_stencil_info.setDepthCompareOp(vk::CompareOp::eLess);
    depth_stencil_info.setDepthBoundsTestEnable(VK_FALSE);
    depth_stencil_info.setStencilTestEnable(VK_FALSE);
    vk::PipelineDynamicStateCreateInfo dynamic_state_info;
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    dynamic_state_info.setDynamicStates(dynamic_states);
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(shader_stages);
    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly_info);
    pipeline_info.setPViewportState(&viewport_state_info);
    pipeline_info.setPRasterizationState(&rasterizer_info);
    pipeline_info.setPMultisampleState(&multisampling_info);
    pipeline_info.setPColorBlendState(&color_blending_info);
    pipeline_info.setPDepthStencilState(&depth_stencil_info);
    pipeline_info.setPDynamicState(&dynamic_state_info);
    pipeline_info.setLayout(*pipeline_layout);
    pipeline_info.setRenderPass(*render_pass);
    pipeline_info.setSubpass(0);
    Pipeline graphics_pipeline = Pipeline::create(device, pipeline_info);

    // descriptor set
    vk::DescriptorBufferInfo uniform_buffer_descriptor;
    uniform_buffer_descriptor.setBuffer(*uniform_buffer);
    uniform_buffer_descriptor.setOffset(0);
    uniform_buffer_descriptor.setRange(sizeof(PixelUniformBuffer));
    vk::DescriptorBufferInfo block_model_buffer_descriptor;
    block_model_buffer_descriptor.setBuffer(*block_model_buffer);
    block_model_buffer_descriptor.setOffset(0);
    block_model_buffer_descriptor.setRange(sizeof(glm::mat4) * 4 * 1024);
    vk::WriteDescriptorSet descriptor_writes[2] = {
        vk::WriteDescriptorSet()
            .setDstSet(*descriptor_set)
            .setDstBinding(0)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&uniform_buffer_descriptor),
        vk::WriteDescriptorSet()
            .setDstSet(*descriptor_set)
            .setDstBinding(1)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&block_model_buffer_descriptor)
    };
    device->updateDescriptorSets(descriptor_writes, nullptr);

    // RENDERER
    PixelBlockRenderer renderer;
    renderer.render_pass           = render_pass;
    renderer.graphics_pipeline     = graphics_pipeline;
    renderer.pipeline_layout       = pipeline_layout;
    renderer.vertex_buffer         = vertex_buffer;
    renderer.vertex_staging_buffer = vertex_staging_buffer;
    renderer.uniform_buffer        = uniform_buffer;
    renderer.block_model_buffer    = block_model_buffer;
    renderer.descriptor_set_layout = descriptor_set_layout;
    renderer.descriptor_pool       = descriptor_pool;
    renderer.descriptor_set        = descriptor_set;

    renderer.fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    renderer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);

    command.spawn(std::move(renderer));
    vert_shader_module.destroy(device);
    frag_shader_module.destroy(device);
    geom_shader_module.destroy(device);
}
EPIX_API void systems::create_pixel_pipeline(
    Command command, Query<Get<Device, CommandPool>, With<RenderContext>> query
) {
    if (!query) return;
    auto [device, command_pool] = query.single();

    // BUFFER CREATION
    vk::BufferCreateInfo vertex_buffer_info;
    vertex_buffer_info.setUsage(
        vk::BufferUsageFlagBits::eVertexBuffer |
        vk::BufferUsageFlagBits::eTransferDst
    );
    vertex_buffer_info.setSize(sizeof(PixelVertex) * 4 * 1024 * 1024);
    AllocationCreateInfo vertex_alloc_info;
    vertex_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    vertex_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    Buffer vertex_buffer =
        Buffer::create(device, vertex_buffer_info, vertex_alloc_info);
    vk::BufferCreateInfo uniform_buffer_info;
    uniform_buffer_info.setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
    uniform_buffer_info.setSize(sizeof(PixelUniformBuffer));
    AllocationCreateInfo uniform_alloc_info;
    uniform_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    uniform_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    Buffer uniform_buffer =
        Buffer::create(device, uniform_buffer_info, uniform_alloc_info);
    vk::BufferCreateInfo block_model_buffer_info;
    block_model_buffer_info.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    block_model_buffer_info.setSize(sizeof(glm::mat4) * 4 * 1024);
    AllocationCreateInfo block_model_alloc_info;
    block_model_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    block_model_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    Buffer block_model_buffer =
        Buffer::create(device, block_model_buffer_info, block_model_alloc_info);

    // DESCRIPTOR SET LAYOUT
    DescriptorSetLayout descriptor_set_layout = DescriptorSetLayout::create(
        device, {vk::DescriptorSetLayoutBinding()
                     .setBinding(0)
                     .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eGeometry),
                 vk::DescriptorSetLayoutBinding()
                     .setBinding(1)
                     .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eGeometry)}
    );

    // DESCRIPTOR POOL
    auto pool_sizes = std::vector<vk::DescriptorPoolSize>{
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
    };
    DescriptorPool descriptor_pool = DescriptorPool::create(
        device, vk::DescriptorPoolCreateInfo()
                    .setMaxSets(1)
                    .setPoolSizes(pool_sizes)
                    .setFlags(
                        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBindEXT
                    )
    );

    // DESCRIPTOR SET
    DescriptorSet descriptor_set = DescriptorSet::create(
        device, vk::DescriptorSetAllocateInfo()
                    .setDescriptorPool(descriptor_pool)
                    .setSetLayouts(*descriptor_set_layout)
    );

    // PIPELINE LAYOUT
    PipelineLayout pipeline_layout = PipelineLayout::create(
        device,
        vk::PipelineLayoutCreateInfo().setSetLayouts(*descriptor_set_layout)
    );

    // RENDER PASS
    vk::AttachmentDescription color_attachment;
    color_attachment.setFormat(vk::Format::eB8G8R8A8Srgb);
    color_attachment.setSamples(vk::SampleCountFlagBits::e1);
    color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
    color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
    color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
    color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
    color_attachment.setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal);
    color_attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.setAttachment(0);
    color_attachment_ref.setLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::SubpassDescription subpass;
    subpass.setColorAttachments(color_attachment_ref);
    vk::RenderPassCreateInfo render_pass_info;
    render_pass_info.setAttachments(color_attachment);
    render_pass_info.setSubpasses(subpass);
    RenderPass render_pass = RenderPass::create(device, render_pass_info);

    // SHADER MODULES
    ShaderModule vert_shader_module = ShaderModule::create(
        device, vk::ShaderModuleCreateInfo().setCode(pixel_vk_vertex_spv)
    );
    ShaderModule frag_shader_module = ShaderModule::create(
        device, vk::ShaderModuleCreateInfo().setCode(pixel_vk_fragment_spv)
    );
    ShaderModule geom_shader_module = ShaderModule::create(
        device, vk::ShaderModuleCreateInfo().setCode(pixel_vk_geometry_spv)
    );
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages = {
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(*vert_shader_module)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eGeometry)
            .setModule(*geom_shader_module)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(*frag_shader_module)
            .setPName("main")
    };

    // GRAPHICS PIPELINE
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    vertex_input_info.setVertexBindingDescriptionCount(1);
    vk::VertexInputBindingDescription binding_description;
    binding_description.setBinding(0);
    binding_description.setStride(sizeof(PixelVertex));
    binding_description.setInputRate(vk::VertexInputRate::eVertex);
    vertex_input_info.setPVertexBindingDescriptions(&binding_description);
    vk::VertexInputAttributeDescription attribute_descriptions[3] = {
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(0)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(PixelVertex, color)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(1)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(offsetof(PixelVertex, pos)),
        vk::VertexInputAttributeDescription()
            .setBinding(0)
            .setLocation(2)
            .setFormat(vk::Format::eR32Sint)
            .setOffset(offsetof(PixelVertex, model_index))
    };
    vertex_input_info.setVertexAttributeDescriptions(attribute_descriptions);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
    input_assembly_info.setTopology(vk::PrimitiveTopology::ePointList);
    input_assembly_info.setPrimitiveRestartEnable(VK_FALSE);
    vk::PipelineViewportStateCreateInfo viewport_state_info;
    viewport_state_info.setViewportCount(1);
    viewport_state_info.setScissorCount(1);
    vk::PipelineRasterizationStateCreateInfo rasterizer_info;
    rasterizer_info.setDepthClampEnable(VK_FALSE);
    rasterizer_info.setRasterizerDiscardEnable(VK_FALSE);
    rasterizer_info.setPolygonMode(vk::PolygonMode::eFill);
    rasterizer_info.setLineWidth(1.0f);
    rasterizer_info.setCullMode({});
    rasterizer_info.setFrontFace(vk::FrontFace::eClockwise);
    rasterizer_info.setDepthBiasEnable(VK_FALSE);
    vk::PipelineMultisampleStateCreateInfo multisampling_info;
    multisampling_info.setSampleShadingEnable(VK_FALSE);
    multisampling_info.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );
    color_blend_attachment.setBlendEnable(VK_TRUE);
    color_blend_attachment.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha);
    color_blend_attachment.setDstColorBlendFactor(
        vk::BlendFactor::eOneMinusSrcAlpha
    );
    color_blend_attachment.setColorBlendOp(vk::BlendOp::eAdd);
    color_blend_attachment.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
    color_blend_attachment.setDstAlphaBlendFactor(vk::BlendFactor::eZero);
    color_blend_attachment.setAlphaBlendOp(vk::BlendOp::eAdd);
    vk::PipelineColorBlendStateCreateInfo color_blending_info;
    color_blending_info.setLogicOpEnable(VK_FALSE);
    color_blending_info.setLogicOp(vk::LogicOp::eCopy);
    color_blending_info.setAttachmentCount(1);
    color_blending_info.setPAttachments(&color_blend_attachment);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_info;
    depth_stencil_info.setDepthTestEnable(VK_TRUE);
    depth_stencil_info.setDepthWriteEnable(VK_TRUE);
    depth_stencil_info.setDepthCompareOp(vk::CompareOp::eLess);
    depth_stencil_info.setDepthBoundsTestEnable(VK_FALSE);
    depth_stencil_info.setStencilTestEnable(VK_FALSE);
    vk::PipelineDynamicStateCreateInfo dynamic_state_info;
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    dynamic_state_info.setDynamicStates(dynamic_states);
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setStages(shader_stages);
    pipeline_info.setPVertexInputState(&vertex_input_info);
    pipeline_info.setPInputAssemblyState(&input_assembly_info);
    pipeline_info.setPViewportState(&viewport_state_info);
    pipeline_info.setPRasterizationState(&rasterizer_info);
    pipeline_info.setPMultisampleState(&multisampling_info);
    pipeline_info.setPColorBlendState(&color_blending_info);
    pipeline_info.setPDepthStencilState(&depth_stencil_info);
    pipeline_info.setPDynamicState(&dynamic_state_info);
    pipeline_info.setLayout(*pipeline_layout);
    pipeline_info.setRenderPass(*render_pass);
    pipeline_info.setSubpass(0);
    Pipeline graphics_pipeline = Pipeline::create(device, pipeline_info);

    // descriptor set
    vk::DescriptorBufferInfo uniform_buffer_descriptor;
    uniform_buffer_descriptor.setBuffer(*uniform_buffer);
    uniform_buffer_descriptor.setOffset(0);
    uniform_buffer_descriptor.setRange(sizeof(PixelUniformBuffer));
    vk::DescriptorBufferInfo block_model_buffer_descriptor;
    block_model_buffer_descriptor.setBuffer(*block_model_buffer);
    block_model_buffer_descriptor.setOffset(0);
    block_model_buffer_descriptor.setRange(sizeof(glm::mat4) * 4 * 1024);
    vk::WriteDescriptorSet descriptor_writes[2] = {
        vk::WriteDescriptorSet()
            .setDstSet(*descriptor_set)
            .setDstBinding(0)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&uniform_buffer_descriptor),
        vk::WriteDescriptorSet()
            .setDstSet(*descriptor_set)
            .setDstBinding(1)
            .setDstArrayElement(0)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setPBufferInfo(&block_model_buffer_descriptor)
    };
    device->updateDescriptorSets(descriptor_writes, nullptr);

    // RENDERER
    PixelRenderer renderer;
    renderer.render_pass           = render_pass;
    renderer.graphics_pipeline     = graphics_pipeline;
    renderer.pipeline_layout       = pipeline_layout;
    renderer.vertex_buffer         = vertex_buffer;
    renderer.uniform_buffer        = uniform_buffer;
    renderer.block_model_buffer    = block_model_buffer;
    renderer.descriptor_set_layout = descriptor_set_layout;
    renderer.descriptor_pool       = descriptor_pool;
    renderer.descriptor_set        = descriptor_set;

    renderer.fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    renderer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);

    command.spawn(std::move(renderer));
    vert_shader_module.destroy(device);
    frag_shader_module.destroy(device);
    geom_shader_module.destroy(device);
}
EPIX_API void systems::draw_pixel_blocks_vk(
    Query<Get<Device, CommandPool, Swapchain, Queue>, With<RenderContext>>
        query,
    Query<Get<PixelBlockRenderer>> renderer_query,
    Query<Get<const PixelBlock, const BlockPos2d>> pixel_block_query
) {
    if (!renderer_query) return;
    if (!query) return;
    auto [device, command_pool, swapchain, queue] = query.single();
    auto [renderer]                               = renderer_query.single();
    ZoneScopedN("render::pixel::draw_pixel_blocks_vk");
    PixelUniformBuffer uniform_buffer;
    uniform_buffer.view = glm::mat4(1.0f);
    uniform_buffer.proj = glm::ortho(
        -static_cast<float>(swapchain.extent.width) / 2.0f,
        static_cast<float>(swapchain.extent.width) / 2.0f,
        static_cast<float>(swapchain.extent.height) / 2.0f,
        -static_cast<float>(swapchain.extent.height) / 2.0f, 1000.0f, -1000.0f
    );
    renderer.begin(device, swapchain, queue, uniform_buffer);
    for (auto [block, pos2d] : pixel_block_query.iter()) {
        renderer.draw(block, pos2d);
    }
    renderer.end();
}
EPIX_API void systems::destroy_pixel_block_pipeline(
    Query<Get<Device>, With<RenderContext>> query,
    Query<Get<PixelBlockRenderer>> renderer_query
) {
    if (!query) return;
    if (!renderer_query) return;
    auto [device]   = query.single();
    auto [renderer] = renderer_query.single();
    renderer.render_pass.destroy(device);
    renderer.graphics_pipeline.destroy(device);
    renderer.pipeline_layout.destroy(device);
    renderer.vertex_buffer.destroy(device);
    renderer.vertex_staging_buffer.destroy(device);
    renderer.uniform_buffer.destroy(device);
    renderer.block_model_buffer.destroy(device);
    renderer.descriptor_set_layout.destroy(device);
    renderer.descriptor_set.destroy(device, renderer.descriptor_pool);
    renderer.descriptor_pool.destroy(device);
    renderer.fence.destroy(device);
    if (renderer.framebuffer) renderer.framebuffer.destroy(device);
}
EPIX_API void systems::destroy_pixel_pipeline(
    Query<Get<Device>, With<RenderContext>> query,
    Query<Get<PixelRenderer>> renderer_query
) {
    if (!query) return;
    if (!renderer_query) return;
    auto [device]   = query.single();
    auto [renderer] = renderer_query.single();
    renderer.render_pass.destroy(device);
    renderer.graphics_pipeline.destroy(device);
    renderer.pipeline_layout.destroy(device);
    renderer.vertex_buffer.destroy(device);
    renderer.uniform_buffer.destroy(device);
    renderer.block_model_buffer.destroy(device);
    renderer.descriptor_set_layout.destroy(device);
    renderer.descriptor_set.destroy(device, renderer.descriptor_pool);
    renderer.descriptor_pool.destroy(device);
    renderer.fence.destroy(device);
    if (renderer.framebuffer) renderer.framebuffer.destroy(device);
}

EPIX_API void PixelRenderPlugin::build(App& app) {
    app.add_system(Startup, systems::create_pixel_block_pipeline);
    app.add_system(Startup, systems::create_pixel_pipeline);
    app.add_system(Render, systems::draw_pixel_blocks_vk)
        .before(font::systems::vulkan::draw_text);
    app.add_system(Exit, systems::destroy_pixel_block_pipeline);
    app.add_system(Exit, systems::destroy_pixel_pipeline);
}
}  // namespace epix::render::pixel
#include <stb_image.h>

#include <filesystem>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include "epix/sprite.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::sprite;
using namespace epix::sprite::components;
using namespace epix::sprite::resources;
using namespace epix::sprite::systems;
using namespace epix::prelude;
using namespace epix::render_vk::components;

std::shared_ptr<spdlog::logger> sprite_logger =
    spdlog::default_logger()->clone("sprite");

EPIX_API void systems::create_sprite_renderer_vk(
    Command cmd,
    Query<Get<Device, CommandPool, Queue>, With<RenderContext>> ctx_query
) {
    if (!ctx_query) return;
    auto [device, command_pool, queue] = ctx_query.single();
    vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
    vk::DescriptorSetLayoutBinding descriptor_set_layout_bindings[4] = {
        vk::DescriptorSetLayoutBinding()
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        vk::DescriptorSetLayoutBinding()
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex),
        vk::DescriptorSetLayoutBinding()
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(65536)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding()
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(256)
            .setStageFlags(vk::ShaderStageFlagBits::eFragment)
    };
    descriptor_set_layout_create_info.setBindings(descriptor_set_layout_bindings
    );
    vk::DescriptorBindingFlags descriptor_binding_flags[4] = {
        {},
        vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind,
        vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind,
        vk::DescriptorBindingFlagBits::ePartiallyBound |
            vk::DescriptorBindingFlagBits::eUpdateAfterBind
    };
    vk::DescriptorSetLayoutBindingFlagsCreateInfo
        descriptor_set_layout_binding_flags_create_info;
    descriptor_set_layout_binding_flags_create_info.setBindingFlags(
        descriptor_binding_flags
    );
    descriptor_set_layout_create_info
        .setPNext(&descriptor_set_layout_binding_flags_create_info)
        .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool);
    DescriptorSetLayout sprite_descriptor_set_layout =
        DescriptorSetLayout::create(device, descriptor_set_layout_create_info);
    vk::DescriptorPoolSize descriptor_pool_sizes[4] = {
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(65536),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eSampler)
            .setDescriptorCount(256)
    };
    vk::DescriptorPoolCreateInfo descriptor_pool_create_info;
    descriptor_pool_create_info.setPoolSizes(descriptor_pool_sizes)
        .setMaxSets(1)
        .setFlags(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
            vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind
        );
    DescriptorPool sprite_descriptor_pool =
        DescriptorPool::create(device, descriptor_pool_create_info);
    DescriptorSet sprite_descriptor_set = DescriptorSet::create(
        device, sprite_descriptor_pool, sprite_descriptor_set_layout
    );

    vk::RenderPassCreateInfo render_pass_create_info;
    vk::AttachmentDescription color_attachment;
    color_attachment.setFormat(vk::Format::eB8G8R8A8Srgb)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eLoad)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);
    vk::AttachmentReference color_attachment_ref;
    color_attachment_ref.setAttachment(0).setLayout(
        vk::ImageLayout::eColorAttachmentOptimal
    );
    vk::AttachmentDescription depth_attachment;
    depth_attachment.setFormat(vk::Format::eD32Sfloat)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(vk::AttachmentLoadOp::eClear)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
        .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::AttachmentReference depth_attachment_ref;
    depth_attachment_ref.setAttachment(1).setLayout(
        vk::ImageLayout::eDepthStencilAttachmentOptimal
    );
    vk::SubpassDescription subpass;
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachments(color_attachment_ref)
        .setPDepthStencilAttachment(&depth_attachment_ref);
    auto attachment_descriptions = {color_attachment, depth_attachment};
    render_pass_create_info.setAttachmentCount(2)
        .setAttachments(attachment_descriptions)
        .setSubpassCount(1)
        .setPSubpasses(&subpass);
    vk::SubpassDependency subpass_dependency;
    subpass_dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
        .setSrcAccessMask(vk::AccessFlagBits::eNoneKHR)
        .setDstAccessMask(
            vk::AccessFlagBits::eColorAttachmentRead |
            vk::AccessFlagBits::eColorAttachmentWrite
        );
    render_pass_create_info.setDependencies(subpass_dependency);
    RenderPass sprite_render_pass =
        RenderPass::create(device, render_pass_create_info);

    vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
    pipeline_layout_create_info.setSetLayoutCount(1).setSetLayouts(
        *sprite_descriptor_set_layout
    );
    PipelineLayout sprite_pipeline_layout =
        PipelineLayout::create(device, pipeline_layout_create_info);

    vk::ShaderModuleCreateInfo vert_shader_module_info;
    vert_shader_module_info.setCode(sprite_vk_vertex_spv);
    ShaderModule vert_shader_module =
        ShaderModule::create(device, vert_shader_module_info);
    vk::ShaderModuleCreateInfo frag_shader_module_info;
    frag_shader_module_info.setCode(sprite_vk_fragment_spv);
    ShaderModule frag_shader_module =
        ShaderModule::create(device, frag_shader_module_info);
    std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages = {
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(vert_shader_module)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo()
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(frag_shader_module)
            .setPName("main")
    };
    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state;
    input_assembly_state.setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(VK_FALSE);
    vk::PipelineDynamicStateCreateInfo dynamic_state;
    std::array<vk::DynamicState, 2> dynamic_states = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    dynamic_state.setDynamicStates(dynamic_states);
    vk::PipelineRasterizationStateCreateInfo rasterization_state;
    rasterization_state.setDepthClampEnable(VK_FALSE)
        .setRasterizerDiscardEnable(VK_FALSE)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_FALSE);
    vk::PipelineColorBlendAttachmentState color_blend_attachment;
    color_blend_attachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );
    color_blend_attachment.setBlendEnable(VK_TRUE);
    color_blend_attachment.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
        .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
        .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
        .setAlphaBlendOp(vk::BlendOp::eAdd);
    vk::PipelineColorBlendStateCreateInfo color_blend_state;
    color_blend_state.setLogicOpEnable(VK_FALSE)
        .setLogicOp(vk::LogicOp::eCopy)
        .setAttachments(color_blend_attachment)
        .setBlendConstants({0.0f, 0.0f, 0.0f, 0.0f});
    vk::PipelineMultisampleStateCreateInfo multisample_state;
    multisample_state.setSampleShadingEnable(VK_FALSE).setRasterizationSamples(
        vk::SampleCountFlagBits::e1
    );
    vk::PipelineViewportStateCreateInfo viewport_state;
    viewport_state.setViewportCount(1).setScissorCount(1);
    vk::PipelineVertexInputStateCreateInfo vertex_input_state;
    vk::VertexInputBindingDescription binding_description;
    binding_description.setBinding(0)
        .setStride(sizeof(SpriteVertex))
        .setInputRate(vk::VertexInputRate::eVertex);
    std::array<vk::VertexInputAttributeDescription, 6> attribute_descriptions =
        {vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(0)
             .setFormat(vk::Format::eR32G32B32Sfloat)
             .setOffset(offsetof(SpriteVertex, pos)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(1)
             .setFormat(vk::Format::eR32G32Sfloat)
             .setOffset(offsetof(SpriteVertex, tex_coord)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(2)
             .setFormat(vk::Format::eR32G32B32A32Sfloat)
             .setOffset(offsetof(SpriteVertex, color)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(3)
             .setFormat(vk::Format::eR32Sint)
             .setOffset(offsetof(SpriteVertex, model_index)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(4)
             .setFormat(vk::Format::eR32Sint)
             .setOffset(offsetof(SpriteVertex, image_index)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(5)
             .setFormat(vk::Format::eR32Sint)
             .setOffset(offsetof(SpriteVertex, sampler_index))};
    vertex_input_state.setVertexBindingDescriptions({binding_description})
        .setVertexAttributeDescriptions(attribute_descriptions);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;
    depth_stencil_state.setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess)
        .setDepthBoundsTestEnable(VK_FALSE)
        .setStencilTestEnable(VK_FALSE);
    vk::GraphicsPipelineCreateInfo pipeline_create_info;
    pipeline_create_info.setStages(shader_stages)
        .setPVertexInputState(&vertex_input_state)
        .setPInputAssemblyState(&input_assembly_state)
        .setPViewportState(&viewport_state)
        .setPRasterizationState(&rasterization_state)
        .setPMultisampleState(&multisample_state)
        .setPColorBlendState(&color_blend_state)
        .setPDepthStencilState(&depth_stencil_state)
        .setPDynamicState(&dynamic_state)
        .setLayout(sprite_pipeline_layout)
        .setRenderPass(sprite_render_pass)
        .setSubpass(0);
    Pipeline sprite_pipeline = Pipeline::create(device, pipeline_create_info);
    vert_shader_module.destroy(device);
    frag_shader_module.destroy(device);

    vk::BufferCreateInfo sprite_uniform_buffer_create_info;
    sprite_uniform_buffer_create_info.setSize(sizeof(glm::mat4) * 2)
        .setUsage(vk::BufferUsageFlagBits::eUniformBuffer);
    AllocationCreateInfo sprite_uniform_buffer_alloc_info;
    sprite_uniform_buffer_alloc_info
        .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    Buffer sprite_uniform_buffer = Buffer::create(
        device, sprite_uniform_buffer_create_info,
        sprite_uniform_buffer_alloc_info
    );
    vk::BufferCreateInfo sprite_vertex_buffer_create_info;
    sprite_vertex_buffer_create_info.setSize(sizeof(SpriteVertex) * 65536)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
    AllocationCreateInfo sprite_vertex_buffer_alloc_info;
    sprite_vertex_buffer_alloc_info
        .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    Buffer sprite_vertex_buffer = Buffer::create(
        device, sprite_vertex_buffer_create_info,
        sprite_vertex_buffer_alloc_info
    );
    vk::BufferCreateInfo sprite_model_buffer_create_info;
    sprite_model_buffer_create_info.setSize(sizeof(glm::mat4) * 65536)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    AllocationCreateInfo sprite_model_buffer_alloc_info;
    sprite_model_buffer_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    Buffer sprite_model_buffer = Buffer::create(
        device, sprite_model_buffer_create_info, sprite_model_buffer_alloc_info
    );
    vk::BufferCreateInfo sprite_index_buffer_create_info;
    sprite_index_buffer_create_info.setSize(sizeof(uint32_t) * 65536)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer);
    AllocationCreateInfo sprite_index_buffer_alloc_info;
    sprite_index_buffer_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    Buffer sprite_index_buffer = Buffer::create(
        device, sprite_index_buffer_create_info, sprite_index_buffer_alloc_info
    );

    vk::DescriptorBufferInfo sprite_uniform_buffer_info;
    sprite_uniform_buffer_info.setBuffer(*sprite_uniform_buffer)
        .setOffset(0)
        .setRange(sizeof(glm::mat4) * 2);
    vk::WriteDescriptorSet uniform_buffer_write;
    uniform_buffer_write.setDstSet(*sprite_descriptor_set)
        .setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setPBufferInfo(&sprite_uniform_buffer_info);
    vk::DescriptorBufferInfo sprite_model_buffer_info;
    sprite_model_buffer_info.setBuffer(*sprite_model_buffer)
        .setOffset(0)
        .setRange(sizeof(glm::mat4) * 65536);
    vk::WriteDescriptorSet model_buffer_write;
    model_buffer_write.setDstSet(*sprite_descriptor_set)
        .setDstBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setPBufferInfo(&sprite_model_buffer_info);
    std::array<vk::WriteDescriptorSet, 2> descriptor_writes = {
        uniform_buffer_write, model_buffer_write
    };
    device->updateDescriptorSets(descriptor_writes, nullptr);

    SpriteRenderer sprite_renderer;
    sprite_renderer.sprite_descriptor_set_layout = sprite_descriptor_set_layout;
    sprite_renderer.sprite_descriptor_pool       = sprite_descriptor_pool;
    sprite_renderer.sprite_descriptor_set        = sprite_descriptor_set;
    sprite_renderer.sprite_render_pass           = sprite_render_pass;
    sprite_renderer.sprite_pipeline_layout       = sprite_pipeline_layout;
    sprite_renderer.sprite_pipeline              = sprite_pipeline;
    sprite_renderer.sprite_uniform_buffer        = sprite_uniform_buffer;
    sprite_renderer.sprite_vertex_buffer         = sprite_vertex_buffer;
    sprite_renderer.sprite_index_buffer          = sprite_index_buffer;
    sprite_renderer.sprite_model_buffer          = sprite_model_buffer;
    sprite_renderer.fence                        = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    sprite_renderer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);
    cmd.spawn(sprite_renderer);
}

EPIX_API void systems::update_image_bindings(
    Query<Get<ImageBindingUpdate>> query,
    Query<Get<ImageView, ImageIndex>, With<Image>> image_query,
    Query<Get<SpriteRenderer>> renderer_query,
    Query<Get<Device>, With<RenderContext>> ctx_query
) {
    if (!renderer_query) return;
    if (!ctx_query) return;
    auto [device]   = ctx_query.single();
    auto [renderer] = renderer_query.single();
    ZoneScopedN("render::sprite::update_image_bindings");
    for (auto [update] : query.iter()) {
        auto [image_view, image_index_t] = image_query.get(update.image_view);
        auto image_index                 = image_index_t.index;
        vk::DescriptorImageInfo image_info;
        image_info.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImageView(*image_view);
        vk::WriteDescriptorSet write_descriptor_set;
        write_descriptor_set.setDstSet(*renderer.sprite_descriptor_set)
            .setDstBinding(2)
            .setDstArrayElement(image_index)
            .setDescriptorType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(1)
            .setPImageInfo(&image_info);
        device->updateDescriptorSets(write_descriptor_set, nullptr);
    }
}

EPIX_API void systems::update_sampler_bindings(
    Query<Get<SamplerBindingUpdate>> query,
    Query<Get<Sampler, SamplerIndex>> sampler_query,
    Query<Get<SpriteRenderer>> renderer_query,
    Query<Get<Device>, With<RenderContext>> ctx_query
) {
    if (!renderer_query) return;
    if (!ctx_query) return;
    auto [device]   = ctx_query.single();
    auto [renderer] = renderer_query.single();
    ZoneScopedN("render::sprite::update_sampler_bindings");
    for (auto [update] : query.iter()) {
        auto [sampler, sampler_index_t] = sampler_query.get(update.sampler);
        auto sampler_index              = sampler_index_t.index;
        vk::DescriptorImageInfo sampler_info;
        sampler_info.setSampler(*sampler);
        vk::WriteDescriptorSet write_descriptor_set;
        write_descriptor_set.setDstSet(*renderer.sprite_descriptor_set)
            .setDstBinding(3)
            .setDstArrayElement(sampler_index)
            .setDescriptorType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1)
            .setPImageInfo(&sampler_info);
        device->updateDescriptorSets(write_descriptor_set, nullptr);
    }
}

EPIX_API void systems::create_sprite_depth_vk(
    Command cmd,
    Query<Get<Device, CommandPool, Queue, Swapchain>, With<RenderContext>>
        ctx_query,
    Query<Get<Image, ImageView>, With<SpriteDepth, SpriteDepthExtent>> query
) {
    if (!ctx_query) return;
    auto [device, command_pool, queue, swapchain] = ctx_query.single();
    if (query) return;
    vk::ImageCreateInfo depth_image_create_info;
    depth_image_create_info.setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eD32Sfloat)
        .setExtent(vk::Extent3D(swapchain.extent, 1))
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);
    AllocationCreateInfo depth_image_alloc_info;
    depth_image_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
    Image depth_image =
        Image::create(device, depth_image_create_info, depth_image_alloc_info);
    vk::ImageViewCreateInfo depth_image_view_create_info;
    depth_image_view_create_info.setImage(*depth_image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eD32Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange()
                                 .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                                 .setBaseMipLevel(0)
                                 .setLevelCount(1)
                                 .setBaseArrayLayer(0)
                                 .setLayerCount(1));
    ImageView depth_image_view =
        ImageView::create(device, depth_image_view_create_info);
    cmd.spawn(
        depth_image_view, depth_image,
        SpriteDepthExtent{swapchain.extent.width, swapchain.extent.height},
        SpriteDepth{}
    );
}

EPIX_API void systems::update_sprite_depth_vk(
    Query<Get<ImageView, Image, SpriteDepthExtent>, With<SpriteDepth>> query,
    Query<Get<Device, Swapchain>, With<RenderContext>> ctx_query
) {
    if (!ctx_query) return;
    if (!query) return;
    auto [image_view, image, extent] = query.single();
    auto [device, swapchain]         = ctx_query.single();
    ZoneScopedN("render::sprite::update_sprite_depth_vk");
    if (extent.width != swapchain.extent.width ||
        extent.height != swapchain.extent.height) {
        vk::ImageCreateInfo depth_image_create_info;
        depth_image_create_info.setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setExtent(vk::Extent3D(swapchain.extent, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);
        AllocationCreateInfo depth_image_alloc_info;
        depth_image_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        Image depth_image = Image::create(
            device, depth_image_create_info, depth_image_alloc_info
        );
        vk::ImageViewCreateInfo depth_image_view_create_info;
        depth_image_view_create_info.setImage(*depth_image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setSubresourceRange(
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            );
        ImageView depth_image_view =
            ImageView::create(device, depth_image_view_create_info);
        image_view.destroy(device);
        image.destroy(device);
        *(&image_view) = depth_image_view;
        *(&image)      = depth_image;
        *(&extent) =
            SpriteDepthExtent{swapchain.extent.width, swapchain.extent.height};
    }
}

EPIX_API void systems::destroy_sprite_depth_vk(
    Query<Get<Device>, With<RenderContext>> query,
    Query<Get<ImageView, Image>, With<SpriteDepth, SpriteDepthExtent>>
        depth_query
) {
    if (!query) return;
    auto [device] = query.single();
    for (auto [image_view, image] : depth_query.iter()) {
        image_view.destroy(device);
        image.destroy(device);
    }
}

EPIX_API void systems::draw_sprite_2d_vk(
    Query<Get<SpriteRenderer>> renderer_query,
    Query<Get<Sprite, SpritePos2D>> sprite_query,
    Query<Get<Device, CommandPool, Queue, Swapchain>, With<RenderContext>>
        ctx_query,
    Query<Get<ImageIndex>, With<Image, ImageView>> image_query,
    Query<Get<SamplerIndex>, With<Sampler>> sampler_query,
    Query<Get<ImageView>, With<SpriteDepth, Image, SpriteDepthExtent>>
        depth_query
) {
    if (!renderer_query) return;
    if (!ctx_query) return;
    if (!depth_query) return;
    auto [depth_image_view]                       = depth_query.single();
    auto [device, command_pool, queue, swapchain] = ctx_query.single();
    auto [renderer]                               = renderer_query.single();
    ZoneScopedN("render::sprite::draw_sprite_2d_vk");
    void* uniform_buffer_data = renderer.sprite_uniform_buffer.map(device);
    glm::mat4* uniform_buffer = static_cast<glm::mat4*>(uniform_buffer_data);
    uniform_buffer[1]         = glm::ortho(
        -static_cast<float>(swapchain.extent.width) / 2.0f,
        static_cast<float>(swapchain.extent.width) / 2.0f,
        static_cast<float>(swapchain.extent.height) / 2.0f,
        -static_cast<float>(swapchain.extent.height) / 2.0f, 1000.0f, -1000.0f
    );

    uniform_buffer[0] = glm::mat4(1.0f);
    renderer.sprite_uniform_buffer.unmap(device);
    glm::mat4* model_buffer_data =
        static_cast<glm::mat4*>(renderer.sprite_model_buffer.map(device));
    int models_count = 0;
    SpriteVertex* vertex_buffer_data =
        static_cast<SpriteVertex*>(renderer.sprite_vertex_buffer.map(device));
    int vertices_count = 0;
    uint16_t* index_buffer_data =
        static_cast<uint16_t*>(renderer.sprite_index_buffer.map(device));
    int indices_count = 0;
    std::vector<std::tuple<Sprite*, SpritePos2D*>> sprites;
    for (auto [sprite, pos_2d] : sprite_query.iter()) {
        sprites.push_back({&sprite, &pos_2d});
    }
    std::sort(sprites.begin(), sprites.end(), [](const auto& a, const auto& b) {
        auto [sprite_a, pos_2d_a] = a;
        auto [sprite_b, pos_2d_b] = b;
        return pos_2d_a->pos.z > pos_2d_b->pos.z;
    });
    for (auto [sprite_p, pos_2d_p] : sprites) {
        auto& sprite    = *sprite_p;
        auto& pos_2d    = *pos_2d_p;
        glm::mat4 model = glm::mat4(1.0f);
        model           = glm::translate(model, pos_2d.pos);
        model =
            glm::rotate(model, pos_2d.rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(pos_2d.scale, 1.0f));
        models_count++;
        if (models_count > 65536) {
            sprite_logger->error("Too many sprites to draw.");
            break;
        }
        *model_buffer_data = model;
        model_buffer_data++;

        if (!sprite.image) {
            sprite_logger->error("Sprite does not have an image assigned.");
            continue;
        }
        if (!sprite.sampler) {
            sprite_logger->error("Sprite does not have a sampler assigned.");
            continue;
        }
        auto [image_index]   = image_query.get(sprite.image);
        auto [sampler_index] = sampler_query.get(sprite.sampler);

        if (vertices_count + 4 > 65536) {
            vertices_count -= 4;
            sprite_logger->error("Too many vertices to draw.");
            break;
        }

        vertex_buffer_data->pos = -glm::vec3(sprite.size * sprite.center, 0.0f);
        vertex_buffer_data->tex_coord     = {0.0f, 0.0f};
        vertex_buffer_data->color         = sprite.color;
        vertex_buffer_data->model_index   = models_count - 1;
        vertex_buffer_data->image_index   = image_index.index;
        vertex_buffer_data->sampler_index = sampler_index.index;
        vertex_buffer_data++;
        vertex_buffer_data->pos = glm::vec3(
            sprite.size.x * (1.0f - sprite.center.x),
            -sprite.size.y * sprite.center.y, 0.0f
        );
        vertex_buffer_data->tex_coord     = {1.0f, 0.0f};
        vertex_buffer_data->color         = sprite.color;
        vertex_buffer_data->model_index   = models_count - 1;
        vertex_buffer_data->image_index   = image_index.index;
        vertex_buffer_data->sampler_index = sampler_index.index;
        vertex_buffer_data++;
        vertex_buffer_data->pos = glm::vec3(
            sprite.size.x * (1.0f - sprite.center.x),
            sprite.size.y * (1.0f - sprite.center.y), 0.0f
        );
        vertex_buffer_data->tex_coord     = {1.0f, 1.0f};
        vertex_buffer_data->color         = sprite.color;
        vertex_buffer_data->model_index   = models_count - 1;
        vertex_buffer_data->image_index   = image_index.index;
        vertex_buffer_data->sampler_index = sampler_index.index;
        vertex_buffer_data++;
        vertex_buffer_data->pos = glm::vec3(
            -sprite.size.x * sprite.center.x,
            sprite.size.y * (1.0f - sprite.center.y), 0.0f
        );
        vertex_buffer_data->tex_coord     = {0.0f, 1.0f};
        vertex_buffer_data->color         = sprite.color;
        vertex_buffer_data->model_index   = models_count - 1;
        vertex_buffer_data->image_index   = image_index.index;
        vertex_buffer_data->sampler_index = sampler_index.index;
        vertex_buffer_data++;
        vertices_count += 4;

        if (indices_count + 6 > 65536) {
            sprite_logger->error("Too many indices to draw.");
            break;
        }
        *index_buffer_data = vertices_count - 4;
        index_buffer_data++;
        *index_buffer_data = vertices_count - 3;
        index_buffer_data++;
        *index_buffer_data = vertices_count - 2;
        index_buffer_data++;
        *index_buffer_data = vertices_count - 2;
        index_buffer_data++;
        *index_buffer_data = vertices_count - 1;
        index_buffer_data++;
        *index_buffer_data = vertices_count - 4;
        index_buffer_data++;
        indices_count += 6;
    }
    renderer.sprite_model_buffer.unmap(device);
    renderer.sprite_vertex_buffer.unmap(device);
    renderer.sprite_index_buffer.unmap(device);

    if (indices_count == 0) return;

    auto& command_buffer = renderer.command_buffer;
    auto& fence          = renderer.fence;
    device->waitForFences(*fence, VK_TRUE, UINT64_MAX);
    device->resetFences(*fence);
    command_buffer->reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    renderer.frame_buffer.destroy(device);
    vk::CommandBufferBeginInfo begin_info;
    command_buffer->begin(begin_info);
    std::array<vk::ImageView, 2> attachments = {
        swapchain.current_image_view(), depth_image_view
    };
    vk::FramebufferCreateInfo frame_buffer_create_info;
    frame_buffer_create_info.setRenderPass(*renderer.sprite_render_pass)
        .setAttachments(attachments)
        .setWidth(swapchain.extent.width)
        .setHeight(swapchain.extent.height)
        .setLayers(1);
    Framebuffer frame_buffer =
        Framebuffer::create(device, frame_buffer_create_info);
    renderer.frame_buffer = frame_buffer;
    vk::RenderPassBeginInfo render_pass_begin_info;
    render_pass_begin_info.setRenderPass(*renderer.sprite_render_pass)
        .setFramebuffer(*frame_buffer)
        .setRenderArea({{0, 0}, swapchain.extent});
    vk::ClearValue clear_values[2] = {
        vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}),
        vk::ClearDepthStencilValue(1.0f, 0)
    };
    render_pass_begin_info.setClearValues(clear_values);
    command_buffer->beginRenderPass(
        render_pass_begin_info, vk::SubpassContents::eInline
    );
    command_buffer->bindPipeline(
        vk::PipelineBindPoint::eGraphics, *renderer.sprite_pipeline
    );
    command_buffer->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *renderer.sprite_pipeline_layout, 0,
        *renderer.sprite_descriptor_set, {}
    );
    command_buffer->setViewport(
        0, vk::Viewport()
               .setWidth(swapchain.extent.width)
               .setHeight(swapchain.extent.height)
               .setMinDepth(0.0f)
               .setMaxDepth(1.0f)
               .setX(0.0f)
               .setY(0.0f)
    );
    command_buffer->setScissor(
        0, vk::Rect2D().setOffset({0, 0}).setExtent(swapchain.extent)
    );
    command_buffer->bindVertexBuffers(0, *renderer.sprite_vertex_buffer, {0});
    command_buffer->bindIndexBuffer(
        *renderer.sprite_index_buffer, 0, vk::IndexType::eUint16
    );
    command_buffer->drawIndexed(indices_count, 1, 0, 0, 0);
    command_buffer->endRenderPass();
    command_buffer->end();
    vk::SubmitInfo submit_info;
    submit_info.setCommandBuffers(*command_buffer);
    queue->submit(submit_info, *fence);
}

EPIX_API void systems::destroy_sprite_renderer_vk(
    Query<Get<SpriteRenderer>> renderer_query,
    Query<Get<Device>, With<RenderContext>> query
) {
    if (!renderer_query) return;
    if (!query) return;
    auto [device]   = query.single();
    auto [renderer] = renderer_query.single();
    renderer.sprite_descriptor_set_layout.destroy(device);
    renderer.sprite_descriptor_pool.destroy(device);
    renderer.sprite_render_pass.destroy(device);
    renderer.sprite_pipeline_layout.destroy(device);
    renderer.sprite_pipeline.destroy(device);
    renderer.sprite_uniform_buffer.destroy(device);
    renderer.sprite_vertex_buffer.destroy(device);
    renderer.sprite_index_buffer.destroy(device);
    renderer.sprite_model_buffer.destroy(device);
    renderer.fence.destroy(device);
    if (renderer.frame_buffer) renderer.frame_buffer.destroy(device);
}
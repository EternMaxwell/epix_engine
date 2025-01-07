#include <unordered_map>

#include "epix/font.h"
#include "shaders/fragment_shader.h"
#include "shaders/geometry_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix;
using namespace epix::font;
using namespace epix::font::components;
using namespace epix::font::systems;
using namespace systems::vulkan;

static std::shared_ptr<spdlog::logger> logger =
    spdlog::default_logger()->clone("font");

EPIX_API void TextRenderer::begin(
    Device* device,
    Swapchain* swapchain,
    Queue* queue,
    CommandPool* command_pool,
    ResMut<resources::vulkan::FT2Library> ft2_library
) {
    if (ctx.has_value()) return;
    ctx = Context{device, queue, swapchain, command_pool, ft2_library};
    reset_cmd();
    framebuffer.destroy(*device);
    framebuffer = Framebuffer::create(
        *device, vk::FramebufferCreateInfo()
                     .setRenderPass(*text_render_pass)
                     .setAttachments(*swapchain->current_image_view())
                     .setWidth(swapchain->extent.width)
                     .setHeight(swapchain->extent.height)
                     .setLayers(1)
    );
    auto& extent        = swapchain->extent;
    glm::mat4 inverse_y = glm::scale(glm::mat4(1.0f), {1.0f, -1.0f, 1.0f});
    glm::mat4 proj =
        inverse_y *
        glm::ortho(0.0f, (float)extent.width, 0.0f, (float)extent.height);
    glm::mat4 view = glm::mat4(1.0f);
    TextUniformBuffer text_uniform;
    text_uniform.view = view;
    text_uniform.proj = proj;
    void* data        = text_uniform_buffer.map(*device);
    memcpy(data, &text_uniform, sizeof(TextUniformBuffer));
    text_uniform_buffer.unmap(*device);
    ctx.value().vertices = (TextVertex*)text_vertex_buffer.map(*device);
    ctx.value().models   = (TextModelData*)text_model_buffer.map(*device);
}

EPIX_API void TextRenderer::reset_cmd() {
    if (!ctx.has_value()) return;
    auto& device = *ctx.value().device;
    device->waitForFences(*fence, VK_TRUE, UINT64_MAX);
    command_buffer->reset(vk::CommandBufferResetFlagBits::eReleaseResources);
}

EPIX_API void TextRenderer::flush() {
    if (!ctx.has_value()) return;
    auto& device = *ctx.value().device;
    auto& queue  = *ctx.value().queue;
    if (ctx.value().vertex_count == 0 || ctx.value().model_count == 0) return;
    command_buffer->begin(vk::CommandBufferBeginInfo{});
    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::eColorAttachmentOptimal);
    barrier.setNewLayout(vk::ImageLayout::eColorAttachmentOptimal);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setImage(*ctx.value().swapchain->current_image());
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );
    barrier.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
    barrier.setDstAccessMask(
        vk::AccessFlagBits::eMemoryRead |
        vk::AccessFlagBits::eColorAttachmentWrite
    );
    command_buffer->pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eBottomOfPipe |
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
        {}, {}, {}, barrier
    );
    command_buffer->beginRenderPass(
        vk::RenderPassBeginInfo()
            .setRenderPass(*text_render_pass)
            .setFramebuffer(*framebuffer)
            .setRenderArea({{0, 0}, ctx.value().swapchain->extent})
            .setClearValues(vk::ClearValue().setColor(
                std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}
            )),
        vk::SubpassContents::eInline
    );
    command_buffer->bindPipeline(
        vk::PipelineBindPoint::eGraphics, *text_pipeline
    );
    std::vector<vk::DescriptorSet> sets = {
        *text_descriptor_set,
        *ctx.value().ft2_library->font_texture_descriptor_set
    };
    command_buffer->bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *text_pipeline_layout, 0, sets, {}
    );
    command_buffer->bindVertexBuffers(0, *text_vertex_buffer, {0});
    command_buffer->setViewport(
        0, vk::Viewport()
               .setWidth((float)ctx.value().swapchain->extent.width)
               .setHeight((float)ctx.value().swapchain->extent.height)
               .setMinDepth(0.0f)
               .setMaxDepth(1.0f)
    );
    command_buffer->setScissor(
        0,
        vk::Rect2D().setOffset({0, 0}).setExtent(ctx.value().swapchain->extent)
    );
    command_buffer->draw(ctx.value().vertex_count, 1, 0, 0);
    command_buffer->endRenderPass();
    command_buffer->end();
    device->resetFences(*fence);
    queue->submit(vk::SubmitInfo().setCommandBuffers(*command_buffer), *fence);
    ctx.value().vertex_count = 0;
    ctx.value().model_count  = 0;
}

EPIX_API void TextRenderer::setModel(const TextModelData& model) {
    if (!ctx.has_value()) return;
    if (ctx.value().model_count >= 1024) {
        flush();
        reset_cmd();
    }
    ctx.value().models[ctx.value().model_count] = model;
    ctx.value().model_count++;
}

EPIX_API void TextRenderer::setModel(const TextPos& pos, int texture_id) {
    setModel(
        {glm::translate(glm::mat4(1.0f), {pos.x, pos.y, 0.0f}),
         {1.0f, 1.0f, 1.0f, 1.0f},
         texture_id}
    );
}

EPIX_API void TextRenderer::draw(const Text& text, const TextPos& pos) {
    if (!ctx.has_value()) return;
    auto& context = ctx.value();
    auto&& tmp    = context.ft2_library->get_font_texture(
        text.font, *context.device, *context.command_pool, *context.queue
    );
    float ax       = 0;
    float ay       = 0;
    int texture_id = (int)context.ft2_library->font_index(text.font);
    setModel(pos, texture_id);
    for (auto& c : text.text) {
        if (context.vertex_count >= 256 * 4096) {
            flush();
            reset_cmd();
            setModel(pos, texture_id);
        }
        auto&& glyph_opt = context.ft2_library->get_glyph_add(
            text.font, c, *context.device, *context.command_pool, *context.queue
        );
        if (!glyph_opt.has_value()) continue;
        auto&& glyph = glyph_opt.value();
        if (glyph.size.x == 0 || glyph.size.y == 0) continue;
        context.vertices[context.vertex_count] = {
            {ax + glyph.bearing.x, ay - (glyph.size.y - glyph.bearing.y)},
            {glyph.uv_1.x, glyph.uv_1.y, glyph.uv_2.x, glyph.uv_2.y},
            {(float)glyph.size.x, (float)glyph.size.y},
            glyph.image_index,
            context.model_count - 1
        };
        context.vertex_count++;
        ax += glyph.advance.x;
        ay += glyph.advance.y;
    }
}

EPIX_API void TextRenderer::end() {
    if (!ctx.has_value()) return;
    auto& device = *ctx.value().device;
    flush();
    text_vertex_buffer.unmap(device);
    text_model_buffer.unmap(device);
    ctx.reset();
}

EPIX_API void systems::vulkan::insert_ft2_library(
    Command command, Query<Get<Device>, With<RenderContext>> query
) {
    if (!query) return;
    auto [device] = query.single();
    resources::vulkan::FT2Library ft2_library;
    logger->info("inserting ft2 library");
    if (FT_Init_FreeType(&ft2_library.library)) {
        spdlog::error("Failed to initialize FreeType library");
    }
    vk::DescriptorSetLayoutBinding font_texture_descriptor_set_layout_binding;
    font_texture_descriptor_set_layout_binding.setBinding(0);
    font_texture_descriptor_set_layout_binding.setDescriptorType(
        vk::DescriptorType::eSampledImage
    );
    font_texture_descriptor_set_layout_binding.setDescriptorCount(4096);
    font_texture_descriptor_set_layout_binding.setStageFlags(
        vk::ShaderStageFlagBits::eFragment
    );
    ft2_library.font_texture_descriptor_set_layout =
        DescriptorSetLayout::create(
            device, {font_texture_descriptor_set_layout_binding}
        );
    std::vector<vk::DescriptorPoolSize> font_texture_descriptor_pool_size = {
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eSampledImage)
            .setDescriptorCount(4096)
    };
    ft2_library.font_texture_descriptor_pool = DescriptorPool::create(
        device, vk::DescriptorPoolCreateInfo()
                    .setMaxSets(1)
                    .setPoolSizes({font_texture_descriptor_pool_size})
                    .setFlags(
                        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBindEXT
                    )
    );
    ft2_library.font_texture_descriptor_set = DescriptorSet::create(
        device,
        vk::DescriptorSetAllocateInfo()
            .setDescriptorPool(*ft2_library.font_texture_descriptor_pool)
            .setSetLayouts(*ft2_library.font_texture_descriptor_set_layout)
    );
    command.insert_resource(ft2_library);
    logger->info("inserted ft2 library");
}

EPIX_API void systems::vulkan::create_renderer(
    Command command,
    Query<Get<Device, Queue, CommandPool>, With<RenderContext>> query,
    Res<FontPlugin> font_plugin,
    ResMut<resources::vulkan::FT2Library> ft2_library
) {
    if (!query) return;
    logger->info("create text renderer");
    auto [device, queue, command_pool] = query.single();
    auto canvas_width                  = font_plugin->canvas_width;
    auto canvas_height                 = font_plugin->canvas_height;
    TextRenderer text_renderer;
    text_renderer.text_descriptor_set_layout = DescriptorSetLayout::create(
        device, {vk::DescriptorSetLayoutBinding()
                     .setBinding(2)
                     .setDescriptorType(vk::DescriptorType::eSampler)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eFragment),
                 vk::DescriptorSetLayoutBinding()
                     .setBinding(1)
                     .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eGeometry),
                 vk::DescriptorSetLayoutBinding()
                     .setBinding(0)
                     .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                     .setDescriptorCount(1)
                     .setStageFlags(vk::ShaderStageFlagBits::eGeometry)}
    );
    auto pool_sizes = std::vector<vk::DescriptorPoolSize>{
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eSampler)
            .setDescriptorCount(1),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1),
        vk::DescriptorPoolSize()
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(1)
    };
    text_renderer.text_descriptor_pool = DescriptorPool::create(
        device, vk::DescriptorPoolCreateInfo()
                    .setMaxSets(1)
                    .setPoolSizes(pool_sizes)
                    .setFlags(
                        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBindEXT
                    )
    );
    text_renderer.text_descriptor_set = DescriptorSet::create(
        device, vk::DescriptorSetAllocateInfo()
                    .setDescriptorPool(*text_renderer.text_descriptor_pool)
                    .setSetLayouts({*text_renderer.text_descriptor_set_layout})
    );
    vk::BufferCreateInfo text_uniform_buffer_create_info;
    text_uniform_buffer_create_info.setSize(sizeof(TextUniformBuffer));
    text_uniform_buffer_create_info.setUsage(
        vk::BufferUsageFlagBits::eUniformBuffer
    );
    text_uniform_buffer_create_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo text_uniform_buffer_alloc_info;
    text_uniform_buffer_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );
    text_uniform_buffer_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    text_renderer.text_uniform_buffer = Buffer::create(
        device, text_uniform_buffer_create_info, text_uniform_buffer_alloc_info
    );
    vk::BufferCreateInfo buffer_create_info;
    buffer_create_info.setSize(sizeof(TextVertex) * 256 * 4096);
    buffer_create_info.setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
    buffer_create_info.setSharingMode(vk::SharingMode::eExclusive);
    AllocationCreateInfo alloc_info;
    alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    alloc_info.setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    text_renderer.text_vertex_buffer =
        Buffer::create(device, buffer_create_info, alloc_info);
    vk::BufferCreateInfo model_buffer_create_info;
    model_buffer_create_info.setSize(sizeof(TextModelData) * 1024);
    model_buffer_create_info.setUsage(vk::BufferUsageFlagBits::eStorageBuffer);
    AllocationCreateInfo model_alloc_info;
    model_alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    model_alloc_info.setFlags(
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    text_renderer.text_model_buffer =
        Buffer::create(device, model_buffer_create_info, model_alloc_info);

    vk::RenderPassCreateInfo render_pass_info;
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
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass.setColorAttachments(color_attachment_ref);
    render_pass_info.setAttachments({color_attachment});
    render_pass_info.setSubpasses({subpass});
    text_renderer.text_render_pass =
        RenderPass::create(device, render_pass_info);

    vk::GraphicsPipelineCreateInfo pipeline_info;

    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts = {
        *text_renderer.text_descriptor_set_layout,
        *ft2_library->font_texture_descriptor_set_layout
    };
    pipeline_layout_info.setSetLayouts(descriptor_set_layouts);
    text_renderer.text_pipeline_layout =
        PipelineLayout::create(device, pipeline_layout_info);

    pipeline_info.setLayout(*text_renderer.text_pipeline_layout);

    vk::ShaderModuleCreateInfo vert_shader_module_info;
    vert_shader_module_info.setCode(font_vk_vertex_spv);
    vk::ShaderModuleCreateInfo frag_shader_module_info;
    frag_shader_module_info.setCode(font_vk_fragment_spv);
    vk::ShaderModuleCreateInfo geom_shader_module_info;
    geom_shader_module_info.setCode(font_vk_geometry_spv);
    ShaderModule vert_shader_module =
        ShaderModule::create(device, vert_shader_module_info);
    ShaderModule frag_shader_module =
        ShaderModule::create(device, frag_shader_module_info);
    ShaderModule geom_shader_module =
        ShaderModule::create(device, geom_shader_module_info);

    vk::PipelineShaderStageCreateInfo vert_shader_stage_info;
    vert_shader_stage_info.setStage(vk::ShaderStageFlagBits::eVertex);
    vert_shader_stage_info.setModule(vert_shader_module);
    vert_shader_stage_info.setPName("main");

    vk::PipelineShaderStageCreateInfo frag_shader_stage_info;
    frag_shader_stage_info.setStage(vk::ShaderStageFlagBits::eFragment);
    frag_shader_stage_info.setModule(frag_shader_module);
    frag_shader_stage_info.setPName("main");

    vk::PipelineShaderStageCreateInfo geom_shader_stage_info;
    geom_shader_stage_info.setStage(vk::ShaderStageFlagBits::eGeometry);
    geom_shader_stage_info.setModule(geom_shader_module);
    geom_shader_stage_info.setPName("main");

    vk::PipelineShaderStageCreateInfo shader_stages[] = {
        vert_shader_stage_info, frag_shader_stage_info, geom_shader_stage_info
    };

    pipeline_info.setStages(shader_stages);

    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    vk::VertexInputBindingDescription binding_description;
    binding_description.setBinding(0);
    binding_description.setStride(sizeof(TextVertex));
    binding_description.setInputRate(vk::VertexInputRate::eVertex);
    std::array<vk::VertexInputAttributeDescription, 5> attribute_descriptions =
        {vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(0)
             .setFormat(vk::Format::eR32G32B32Sfloat)
             .setOffset(offsetof(TextVertex, pos)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(1)
             .setFormat(vk::Format::eR32G32B32A32Sfloat)
             .setOffset(offsetof(TextVertex, uvs)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(2)
             .setFormat(vk::Format::eR32G32Sfloat)
             .setOffset(offsetof(TextVertex, size)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(3)
             .setFormat(vk::Format::eR32Sint)
             .setOffset(offsetof(TextVertex, image_index)),
         vk::VertexInputAttributeDescription()
             .setBinding(0)
             .setLocation(4)
             .setFormat(vk::Format::eR32Sint)
             .setOffset(offsetof(TextVertex, model_id))};
    vertex_input_info.setVertexBindingDescriptions({binding_description});
    vertex_input_info.setVertexAttributeDescriptions(attribute_descriptions);

    pipeline_info.setPVertexInputState(&vertex_input_info);

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_info;
    input_assembly_info.setTopology(vk::PrimitiveTopology::ePointList);
    input_assembly_info.setPrimitiveRestartEnable(VK_FALSE);

    pipeline_info.setPInputAssemblyState(&input_assembly_info);

    vk::DynamicState dynamic_states[] = {
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state_info;
    dynamic_state_info.setDynamicStates(dynamic_states);
    pipeline_info.setPDynamicState(&dynamic_state_info);

    vk::PipelineViewportStateCreateInfo viewport_state_info;
    viewport_state_info.setViewportCount(1);
    viewport_state_info.setScissorCount(1);
    pipeline_info.setPViewportState(&viewport_state_info);

    vk::PipelineRasterizationStateCreateInfo rasterizer_info;
    rasterizer_info.setDepthClampEnable(VK_FALSE);
    rasterizer_info.setRasterizerDiscardEnable(VK_FALSE);
    rasterizer_info.setPolygonMode(vk::PolygonMode::eFill);
    rasterizer_info.setLineWidth(1.0f);
    rasterizer_info.setCullMode({});
    rasterizer_info.setFrontFace(vk::FrontFace::eClockwise);
    rasterizer_info.setDepthBiasEnable(VK_FALSE);
    pipeline_info.setPRasterizationState(&rasterizer_info);

    vk::PipelineMultisampleStateCreateInfo multisampling_info;
    multisampling_info.setSampleShadingEnable(VK_FALSE);
    multisampling_info.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    pipeline_info.setPMultisampleState(&multisampling_info);

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
    pipeline_info.setPColorBlendState(&color_blending_info);

    pipeline_info.setRenderPass(*text_renderer.text_render_pass);
    pipeline_info.setSubpass(0);

    text_renderer.text_pipeline = Pipeline::create(device, pipeline_info);

    vert_shader_module.destroy(device);
    frag_shader_module.destroy(device);
    geom_shader_module.destroy(device);

    text_renderer.text_texture_sampler = Sampler::create(
        device, vk::SamplerCreateInfo()
                    .setMagFilter(vk::Filter::eNearest)
                    .setMinFilter(vk::Filter::eNearest)
                    .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                    .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                    .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                    .setAnisotropyEnable(VK_TRUE)
                    .setMaxAnisotropy(16)
                    .setBorderColor(vk::BorderColor::eIntTransparentBlack)
                    .setUnnormalizedCoordinates(VK_FALSE)
                    .setCompareEnable(VK_FALSE)
                    .setCompareOp(vk::CompareOp::eAlways)
                    .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                    .setMipLodBias(0.0f)
                    .setMinLod(0.0f)
                    .setMaxLod(0.0f)
    );
    text_renderer.fence = Fence::create(
        device,
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    text_renderer.command_buffer =
        CommandBuffer::allocate_primary(device, command_pool);

    vk::DescriptorBufferInfo buffer_info;
    buffer_info.setBuffer(text_renderer.text_uniform_buffer);
    buffer_info.setOffset(0);
    buffer_info.setRange(sizeof(TextUniformBuffer));
    vk::WriteDescriptorSet descriptor_write;
    descriptor_write.setDstSet(*text_renderer.text_descriptor_set);
    descriptor_write.setDstBinding(0);
    descriptor_write.setDstArrayElement(0);
    descriptor_write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    descriptor_write.setDescriptorCount(1);
    descriptor_write.setPBufferInfo(&buffer_info);
    vk::DescriptorImageInfo image_info;
    image_info.setSampler(*text_renderer.text_texture_sampler);
    vk::WriteDescriptorSet descriptor_write_sampler;
    descriptor_write_sampler.setDstSet(*text_renderer.text_descriptor_set);
    descriptor_write_sampler.setDstBinding(2);
    descriptor_write_sampler.setDstArrayElement(0);
    descriptor_write_sampler.setDescriptorType(vk::DescriptorType::eSampler);
    descriptor_write_sampler.setDescriptorCount(1);
    descriptor_write_sampler.setPImageInfo(&image_info);
    vk::DescriptorBufferInfo model_buffer_info;
    model_buffer_info.setBuffer(text_renderer.text_model_buffer);
    model_buffer_info.setOffset(0);
    model_buffer_info.setRange(sizeof(glm::mat4) * 1024);
    vk::WriteDescriptorSet model_descriptor_write;
    model_descriptor_write.setDstSet(*text_renderer.text_descriptor_set);
    model_descriptor_write.setDstBinding(1);
    model_descriptor_write.setDstArrayElement(0);
    model_descriptor_write.setDescriptorType(vk::DescriptorType::eStorageBuffer
    );
    model_descriptor_write.setDescriptorCount(1);
    model_descriptor_write.setPBufferInfo(&model_buffer_info);
    std::vector<vk::WriteDescriptorSet> descriptor_writes = {
        descriptor_write, descriptor_write_sampler, model_descriptor_write
    };
    device->updateDescriptorSets(descriptor_writes, nullptr);

    command.spawn(text_renderer);
}

EPIX_API void systems::vulkan::draw_text(
    Query<Get<TextRenderer>> text_renderer_query,
    Query<Get<Text, TextPos>> text_query,
    ResMut<resources::vulkan::FT2Library> ft2_library,
    Query<Get<Device, Queue, CommandPool, Swapchain>, With<RenderContext>>
        swapchain_query,
    Res<FontPlugin> font_plugin
) {
    if (!text_renderer_query) return;
    if (!swapchain_query) return;
    auto [device, queue, command_pool, swapchain] = swapchain_query.single();
    auto [text_renderer] = text_renderer_query.single();
    ZoneScopedN("render::font::draw_text");
    text_renderer.begin(
        &device, &swapchain, &queue, &command_pool, ft2_library
    );
    for (auto&& [text, pos] : text_query.iter()) {
        text_renderer.draw(text, pos);
    }
    text_renderer.end();
}

EPIX_API void systems::vulkan::destroy_renderer(
    Query<Get<Device>, With<RenderContext>> query,
    ResMut<resources::vulkan::FT2Library> ft2_library,
    Query<Get<TextRenderer>> text_renderer_query
) {
    if (!text_renderer_query) return;
    if (!query) return;
    auto [device]        = query.single();
    auto [text_renderer] = text_renderer_query.single();
    device->waitForFences(*text_renderer.fence, VK_TRUE, UINT64_MAX);
    logger->debug("destroy text renderer");
    ft2_library->clear_font_textures(device);
    ft2_library->font_texture_descriptor_set.destroy(
        device, ft2_library->font_texture_descriptor_pool
    );
    ft2_library->font_texture_descriptor_pool.destroy(device);
    ft2_library->font_texture_descriptor_set_layout.destroy(device);
    text_renderer.text_render_pass.destroy(device);
    text_renderer.text_pipeline.destroy(device);
    text_renderer.text_descriptor_set.destroy(
        device, text_renderer.text_descriptor_pool
    );
    text_renderer.text_descriptor_pool.destroy(device);
    text_renderer.text_descriptor_set_layout.destroy(device);
    text_renderer.text_pipeline_layout.destroy(device);
    text_renderer.text_uniform_buffer.destroy(device);
    text_renderer.text_vertex_buffer.destroy(device);
    text_renderer.text_model_buffer.destroy(device);
    text_renderer.text_texture_sampler.destroy(device);
    if (text_renderer.framebuffer) text_renderer.framebuffer.destroy(device);
    text_renderer.fence.destroy(device);
    FT_Done_FreeType(ft2_library->library);
}
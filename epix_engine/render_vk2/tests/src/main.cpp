#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/rdvk_res.h>
#include <epix/window.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <pfr.hpp>
#include <spirv_glsl.hpp>

#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::render::vulkan2::backend;

struct test {
    float x;
    float y;
    float z;
};

using namespace epix;

struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct TestPipeline {
    Device device;
    Swapchain swapchain;
    vk::RenderPass render_pass;
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    std::vector<vk::DescriptorSetLayout> set_layouts;

    CommandBuffer command_buffer;
    Fence fence;
    Framebuffer framebuffer = {};

    Buffer vertex_buffer;
    const uint32_t vertex_max_count = 1 * 3;
    Buffer staging_buffer;
    uint32_t staging_buffer_size = 0;

    struct mesh {
        std::vector<Vertex> vertices;
        uint32_t image_index;
        uint32_t sampler_index;
        DescriptorSet descriptor_set;

        mesh(
            uint32_t image_index,
            uint32_t sampler_index,
            const DescriptorSet& descriptor_set
        )
            : image_index(image_index),
              sampler_index(sampler_index),
              descriptor_set(descriptor_set) {}

        void add_quad(
            const glm::vec3& pos, const glm::vec2& size, const glm::vec4& color
        ) {
            vertices.push_back({pos, {0.0f, 0.0f}, color});
            vertices.push_back(
                {pos + glm::vec3{size.x, 0.0f, 0.0f}, {1.0f, 0.0f}, color}
            );
            vertices.push_back(
                {pos + glm::vec3{size.x, size.y, 0.0f}, {1.0f, 1.0f}, color}
            );
            vertices.push_back(
                {pos + glm::vec3{0.0f, size.y, 0.0f}, {0.0f, 1.0f}, color}
            );
            vertices.push_back({pos, {0.0f, 0.0f}, color});
            vertices.push_back(
                {pos + glm::vec3{size.x, size.y, 0.0f}, {1.0f, 1.0f}, color}
            );
        }
    };

    void create_render_pass() {
        vk::AttachmentDescription color_attachment;
        color_attachment.setFormat(swapchain.surface_format.format);
        color_attachment.setSamples(vk::SampleCountFlagBits::e1);
        color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
        color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
        color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        color_attachment.setInitialLayout(
            vk::ImageLayout::eColorAttachmentOptimal
        );
        color_attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::AttachmentReference color_attachment_ref;
        color_attachment_ref.setAttachment(0);
        color_attachment_ref.setLayout(vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::SubpassDescription subpass;
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        subpass.setColorAttachments(color_attachment_ref);
        vk::SubpassDependency dependency;
        dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
        dependency.setDstSubpass(0);
        dependency.setSrcStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );
        dependency.setDstStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );
        dependency.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        dependency.setDependencyFlags(vk::DependencyFlagBits::eByRegion);
        vk::RenderPassCreateInfo render_pass_info;
        render_pass_info.setAttachments(color_attachment);
        render_pass_info.setSubpasses(subpass);
        render_pass_info.setDependencies(dependency);
        render_pass = device.createRenderPass(render_pass_info);
    }
    void create_layout() {
        using namespace epix::render::vulkan2::util;
        vk::PipelineLayoutCreateInfo layout_info;
        auto source_vert = std::vector<uint32_t>(
            vertex_spv, vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
        );
        auto source_frag = std::vector<uint32_t>(
            fragment_spv, fragment_spv + sizeof(fragment_spv) / sizeof(uint32_t)
        );
        vk::ShaderModuleCreateInfo vert_info;
        vert_info.setCode(source_vert);
        vk::ShaderModuleCreateInfo frag_info;
        frag_info.setCode(source_frag);
        spirv_cross::CompilerGLSL vert(source_vert);
        spirv_cross::CompilerGLSL frag(source_frag);
        std::vector<std::vector<vk::DescriptorSetLayoutBinding>> bindings;
        get_shader_resource_bindings(
            bindings, vert, vk::ShaderStageFlagBits::eVertex
        );
        get_shader_resource_bindings(
            bindings, frag, vk::ShaderStageFlagBits::eFragment
        );
        vk::PushConstantRange range;
        if (get_push_constant_ranges(
                range, vert, vk::ShaderStageFlagBits::eVertex
            ) ||
            get_push_constant_ranges(
                range, frag, vk::ShaderStageFlagBits::eFragment
            )) {
            layout_info.setPushConstantRanges(range);
        }
        auto& layouts = set_layouts;
        layouts.reserve(bindings.size());
        for (auto& binding : bindings) {
            vk::DescriptorSetLayoutCreateInfo layout_info;
            layout_info.setBindings(binding);
            layouts.push_back(device.createDescriptorSetLayout(layout_info));
        }
        layout_info.setSetLayouts(layouts);
        layout = device.createPipelineLayout(layout_info);
    }
    void create_pipeline() {
        using namespace epix::render::vulkan2::util;
        auto vert_source = std::vector<uint32_t>(
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
        auto stages        = default_shader_stages(
            vk::ShaderStageFlagBits::eVertex, vert_module,
            vk::ShaderStageFlagBits::eFragment, frag_module
        );
        pipeline_info.setStages(stages);
        pipeline_info.setLayout(layout);
        vk::PipelineViewportStateCreateInfo viewport_state;
        auto view_scissors = default_viewport_scissor(
            &viewport_state, swapchain.others->extent, 1
        );
        pipeline_info.setPViewportState(&viewport_state);
        vk::PipelineInputAssemblyStateCreateInfo input_assembly;
        input_assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
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
        vk::PipelineDynamicStateCreateInfo dynamic_state;
        auto dynamic_states = default_dynamic_states(&dynamic_state);
        pipeline_info.setPDynamicState(&dynamic_state);
        vk::PipelineVertexInputStateCreateInfo vertex_input;
        auto attributes = get_vertex_input_attributes(vert);
        auto bindings   = std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription().setStride(sizeof(Vertex))
        };
        default_vertex_input(&vertex_input, bindings, attributes);
        pipeline_info.setPVertexInputState(&vertex_input);
        pipeline_info.setRenderPass(render_pass);
        pipeline =
            device.createGraphicsPipeline(vk::PipelineCache(), pipeline_info)
                .value;
        device.destroyShaderModule(vert_module);
        device.destroyShaderModule(frag_module);
    }
    void create_buffers() {
        auto vertex_buffer_info =
            vk::BufferCreateInfo()
                .setSize(sizeof(Vertex) * vertex_max_count)
                .setUsage(
                    vk::BufferUsageFlagBits::eVertexBuffer |
                    vk::BufferUsageFlagBits::eTransferDst
                )
                .setSharingMode(vk::SharingMode::eExclusive);
        auto alloc_info =
            AllocationCreateInfo()
                .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        vertex_buffer = device.createBuffer(vertex_buffer_info, alloc_info);
        auto staging_buffer_info =
            vk::BufferCreateInfo()
                .setSize(sizeof(Vertex) * vertex_max_count)
                .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                .setSharingMode(vk::SharingMode::eExclusive);
        staging_buffer_size = vertex_max_count;
        auto staging_alloc_info =
            AllocationCreateInfo()
                .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                );
        staging_buffer =
            device.createBuffer(staging_buffer_info, staging_alloc_info);
    }
    void resize_staging_buffer(uint32_t size) {
        if (size > staging_buffer_size) {
            staging_buffer_size = size;
            device.destroyBuffer(staging_buffer);
            auto staging_buffer_info =
                vk::BufferCreateInfo()
                    .setSize(sizeof(Vertex) * size * 1.5)
                    .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                    .setSharingMode(vk::SharingMode::eExclusive);
            auto staging_alloc_info =
                AllocationCreateInfo()
                    .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                    .setFlags(
                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    );
            staging_buffer =
                device.createBuffer(staging_buffer_info, staging_alloc_info);
        }
    }
    void create_cmd_fence(CommandPool& pool) {
        command_buffer = device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo()
                .setCommandPool(pool)
                .setLevel(vk::CommandBufferLevel::ePrimary)
                .setCommandBufferCount(1)
        )[0];
        fence = device.createFence(
            vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
        );
    }

    void draw_mesh(mesh& mesh) {
        if (mesh.vertices.empty()) {
            return;
        }
        resize_staging_buffer(mesh.vertices.size());
        auto* data = (Vertex*)staging_buffer.map();
        std::memcpy(
            data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex)
        );
        staging_buffer.unmap();
        device.waitForFences(fence, VK_TRUE, UINT64_MAX);
        device.resetFences(fence);
        if (framebuffer) {
            device.destroyFramebuffer(framebuffer);
        }
        framebuffer = device.createFramebuffer(
            vk::FramebufferCreateInfo()
                .setRenderPass(render_pass)
                .setAttachments(swapchain.current_image_view())
                .setWidth(swapchain.others->extent.width)
                .setHeight(swapchain.others->extent.height)
                .setLayers(1)
        );
        auto& cmd = command_buffer;
        cmd.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        cmd.begin(vk::CommandBufferBeginInfo());
        vk::RenderPassBeginInfo render_pass_info;
        render_pass_info.setRenderPass(render_pass);
        render_pass_info.setFramebuffer(framebuffer);
        render_pass_info.setRenderArea(
            vk::Rect2D().setExtent(swapchain.others->extent)
        );
        // std::array<vk::ClearValue, 1> clear_values;
        // clear_values[0].setColor(vk::ClearColorValue().setFloat32({0.0f,
        // 0.0f, 0.0f, 1.0f})); render_pass_info.setClearValues(clear_values);
        uint32_t offset = 0;
        while (offset < mesh.vertices.size()) {
            uint32_t count = std::min(
                (uint32_t)mesh.vertices.size() - offset, vertex_max_count
            );
            vk::BufferCopy copy_region;
            copy_region.setSize(count * sizeof(Vertex));
            copy_region.setSrcOffset(offset * sizeof(Vertex));
            copy_region.setDstOffset(0);
            cmd.copyBuffer(staging_buffer, vertex_buffer, copy_region);
            vk::BufferMemoryBarrier barrier;
            barrier.setBuffer(vertex_buffer);
            barrier.setSize(count * sizeof(Vertex));
            barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
            barrier.setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead);
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eVertexInput, {}, {}, barrier, {}
            );
            cmd.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            vk::DeviceSize boffset = 0;
            cmd.bindVertexBuffers(0, *vertex_buffer, boffset);
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, layout, 0,
                mesh.descriptor_set, {}
            );
            vk::Viewport viewport;
            viewport.setWidth(swapchain.others->extent.width);
            viewport.setHeight(swapchain.others->extent.height);
            cmd.setViewport(0, viewport);
            vk::Rect2D scissor;
            scissor.setExtent(swapchain.others->extent);
            cmd.setScissor(0, scissor);
            int pc[] = {mesh.image_index, mesh.sampler_index};
            cmd.pushConstants(
                layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(pc), &pc
            );
            cmd.draw(count, 1, 0, 0);
            cmd.endRenderPass();
            barrier.setBuffer(vertex_buffer);
            barrier.setSize(count * sizeof(Vertex));
            barrier.setSrcAccessMask(vk::AccessFlagBits::eVertexAttributeRead);
            barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eVertexInput,
                vk::PipelineStageFlagBits::eTransfer, {}, {}, barrier, {}
            );
            offset += vertex_max_count;
        }
        cmd.end();
        vk::SubmitInfo submit_info;
        submit_info.setCommandBuffers(cmd);
        device.getQueue(device.queue_family_index, 0)
            .submit(submit_info, fence);
    }

    void destroy() {
        device.waitForFences(fence, VK_TRUE, UINT64_MAX);
        device.resetFences(fence);
        device.destroyPipeline(pipeline);
        device.destroyPipelineLayout(layout);
        device.destroyRenderPass(render_pass);
        for (auto& layout : set_layouts) {
            device.destroyDescriptorSetLayout(layout);
        }
        device.destroyBuffer(vertex_buffer);
        device.destroyBuffer(staging_buffer);
        device.destroyFence(fence);
        if (framebuffer) {
            device.destroyFramebuffer(framebuffer);
        }
    }
};

void create_pipeline(
    Query<
        Get<Device, Swapchain, CommandPool>,
        With<epix::render::vulkan2::RenderContext>> query,
    Command cmd
) {
    if (!query) {
        return;
    }
    auto [device, swapchain, command_pool] = query.single();
    TestPipeline pipeline;
    pipeline.device    = device;
    pipeline.swapchain = swapchain;
    pipeline.create_render_pass();
    pipeline.create_layout();
    pipeline.create_pipeline();
    pipeline.create_buffers();
    pipeline.create_cmd_fence(command_pool);
    cmd.spawn(pipeline);
}

void extract_pipeline(
    Extract<Get<Entity, TestPipeline>> query,
    Query<Get<Entity>, With<Wrapper<TestPipeline>>> query2,
    Command cmd
) {
    if (!query) {
        return;
    }
    if (!query2) {
        auto [entity, pipeline] = query.single();
        ZoneScopedN("Extract pipeline");
        cmd.spawn(query.wrap(entity));
    }
}

void destroy_pipeline(Query<Get<TestPipeline>> query) {
    if (!query) {
        return;
    }
    auto [pipeline] = query.single();
    pipeline.destroy();
}

void create_sampler(Query<
                    Get<epix::render::vulkan2::ResourceManager>,
                    With<epix::render::vulkan2::RenderContextResManager>> query
) {
    if (!query) {
        return;
    }
    auto [res_manager] = query.single();
    auto& device       = res_manager.device;
    vk::SamplerCreateInfo sampler_info;
    sampler_info.setMagFilter(vk::Filter::eLinear);
    sampler_info.setMinFilter(vk::Filter::eLinear);
    sampler_info.setAddressModeU(vk::SamplerAddressMode::eRepeat);
    sampler_info.setAddressModeV(vk::SamplerAddressMode::eRepeat);
    sampler_info.setAddressModeW(vk::SamplerAddressMode::eRepeat);
    sampler_info.setAnisotropyEnable(true);
    sampler_info.setMaxAnisotropy(16);
    sampler_info.setBorderColor(vk::BorderColor::eIntOpaqueBlack);
    sampler_info.setUnnormalizedCoordinates(false);
    sampler_info.setCompareEnable(false);
    sampler_info.setCompareOp(vk::CompareOp::eAlways);
    sampler_info.setMipmapMode(vk::SamplerMipmapMode::eLinear);
    sampler_info.setMipLodBias(0.0f);
    sampler_info.setMinLod(0.0f);
    sampler_info.setMaxLod(0.0f);
    res_manager.add_sampler(
        "test::rdvk::sampler1", device.createSampler(sampler_info)
    );
}

void create_image_and_view(
    Query<
        Get<epix::render::vulkan2::ResourceManager>,
        With<epix::render::vulkan2::RenderContextResManager>> query,
    Query<Get<Queue, CommandPool>, With<epix::render::vulkan2::RenderContext>>
        query2
) {
    if (!query || !query2) {
        return;
    }
    auto [queue, command_pool] = query2.single();
    auto [res_manager]         = query.single();
    auto& device               = res_manager.device;
    auto image_create_info     = vk::ImageCreateInfo()
                                 .setImageType(vk::ImageType::e2D)
                                 .setFormat(vk::Format::eR8G8B8A8Unorm)
                                 .setExtent(vk::Extent3D(1, 1, 1))
                                 .setMipLevels(1)
                                 .setArrayLayers(1)
                                 .setSamples(vk::SampleCountFlagBits::e1)
                                 .setTiling(vk::ImageTiling::eOptimal)
                                 .setUsage(
                                     vk::ImageUsageFlagBits::eSampled |
                                     vk::ImageUsageFlagBits::eTransferDst
                                 )
                                 .setSharingMode(vk::SharingMode::eExclusive)
                                 .setInitialLayout(vk::ImageLayout::eUndefined);
    auto alloc_info = AllocationCreateInfo()
                          .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                          .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
    auto image           = device.createImage(image_create_info, alloc_info);
    auto stagging_buffer = device.createBuffer(
        vk::BufferCreateInfo()
            .setSize(4)
            .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
            .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
    );
    auto data = (uint8_t*)stagging_buffer.map();
    data[0]   = 255;
    data[1]   = 0;
    data[2]   = 0;
    data[3]   = 255;
    stagging_buffer.unmap();
    auto cmd = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    cmd.begin(vk::CommandBufferBeginInfo());
    vk::ImageMemoryBarrier barrier1;
    barrier1.setOldLayout(vk::ImageLayout::eUndefined);
    barrier1.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
    barrier1.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier1.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier1.setImage(image.image);
    barrier1.setSubresourceRange(
        vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1)
    );
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, {barrier1}
    );
    vk::BufferImageCopy copy_region;
    copy_region.setBufferOffset(0);
    copy_region.setBufferRowLength(0);
    copy_region.setBufferImageHeight(0);
    copy_region.setImageSubresource(
        vk::ImageSubresourceLayers()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(1)
    );
    copy_region.setImageOffset({0, 0, 0});
    copy_region.setImageExtent({1, 1, 1});
    cmd.copyBufferToImage(
        stagging_buffer.buffer, image.image,
        vk::ImageLayout::eTransferDstOptimal, copy_region
    );
    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
    barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setImage(image.image);
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1)
    );
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, {barrier}
    );
    cmd.end();
    auto submit_info = vk::SubmitInfo().setCommandBuffers(cmd);
    auto fence       = device.createFence(vk::FenceCreateInfo());
    queue.submit(submit_info, fence);
    device.waitForFences(fence, VK_TRUE, UINT64_MAX);
    device.destroyFence(fence);
    device.destroyBuffer(stagging_buffer);
    device.freeCommandBuffers(command_pool, cmd);
    auto view = device.createImageView(
        vk::ImageViewCreateInfo()
            .setImage(image.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setComponents(vk::ComponentMapping()
                               .setR(vk::ComponentSwizzle::eIdentity)
                               .setG(vk::ComponentSwizzle::eIdentity)
                               .setB(vk::ComponentSwizzle::eIdentity)
                               .setA(vk::ComponentSwizzle::eIdentity))
            .setSubresourceRange(
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            )
    );
    res_manager.add_image("test::rdvk::image1", image);
    res_manager.add_image_view("test::rdvk::image1::view", view);
}

void test_render(
    Query<
        epix::CWrap<epix::render::vulkan2::ResourceManager>,
        With<epix::render::vulkan2::RenderContextResManager>> query,
    Query<epix::Wrap<TestPipeline>> query2
) {
    if (!query || !query2) {
        return;
    }
    auto [res_manager_ref] = query.single();
    auto [pipeline_ref]    = query2.single();
    auto [res_manager]     = *res_manager_ref;
    auto [pipeline]        = *pipeline_ref;
    ZoneScopedN("Test render");
    TestPipeline::mesh mesh(
        res_manager.image_index("test::rdvk::image1"),
        res_manager.sampler_index("test::rdvk::sampler1"),
        res_manager.get_descriptor_set()
    );
    mesh.add_quad({-0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
    pipeline.draw_mesh(mesh);
}

int main() {
    using namespace epix::app;
    using namespace epix::window;

    App app2 = App::create2();
    app2.add_plugin(WindowPlugin{});
    app2.get_plugin<WindowPlugin>()->primary_desc().set_vsync(false);
    app2.add_plugin(
        epix::render::vulkan2::RenderVKPlugin{}.set_debug_callback(true)
    );
    app2.add_plugin(epix::render::vulkan2::VulkanResManagerPlugin{});
    app2.add_plugin(epix::input::InputPlugin{});
    app2.add_system(
        epix::Startup, create_pipeline, create_sampler, create_image_and_view
    );
    app2.add_system(epix::Extraction, extract_pipeline);
    app2.add_system(epix::Render, test_render);
    app2.add_system(epix::Exit, destroy_pipeline);
    app2.run();
}
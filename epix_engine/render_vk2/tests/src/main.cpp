#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/window.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <pfr.hpp>
#include <spirv_glsl.hpp>

#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::render::vulkan2::backend;
using namespace epix::render::vulkan2;
using epix::render::vulkan2::VulkanResources;

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

struct PushConstants {
    int image_index;
    int sampler_index;
};

using test_mesh    = Mesh<Vertex>;
using staging_mesh = StagingMesh<test_mesh>;
using batch        = Batch<test_mesh, PushConstants>;

struct TestPipeline : public PipelineBase {
    TestPipeline(Device& device) : PipelineBase(device) {
        set_render_pass([](Device& device) {
            vk::AttachmentDescription color_attachment;
            color_attachment.setFormat(vk::Format::eR8G8B8A8Srgb);
            color_attachment.setSamples(vk::SampleCountFlagBits::e1);
            color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
            color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
            color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
            color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare
            );
            color_attachment.setInitialLayout(
                vk::ImageLayout::eColorAttachmentOptimal
            );
            color_attachment.setFinalLayout(
                vk::ImageLayout::eColorAttachmentOptimal
            );
            vk::AttachmentReference color_attachment_ref;
            color_attachment_ref.setAttachment(0);
            color_attachment_ref.setLayout(
                vk::ImageLayout::eColorAttachmentOptimal
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
            dependency.setSrcAccessMask(
                vk::AccessFlagBits::eColorAttachmentWrite
            );
            dependency.setDstAccessMask(
                vk::AccessFlagBits::eColorAttachmentWrite
            );
            dependency.setDependencyFlags(vk::DependencyFlagBits::eByRegion);
            vk::RenderPassCreateInfo render_pass_info;
            render_pass_info.setAttachments(color_attachment);
            render_pass_info.setSubpasses(subpass);
            render_pass_info.setDependencies(dependency);
            return device.createRenderPass(render_pass_info);
        });
        set_descriptor_pool([](Device& device) {
            std::vector<vk::DescriptorPoolSize> pool_size{
                vk::DescriptorPoolSize()
                    .setType(vk::DescriptorType::eSampledImage)
                    .setDescriptorCount(65536),
                vk::DescriptorPoolSize()
                    .setType(vk::DescriptorType::eSampler)
                    .setDescriptorCount(65536),
            };
            vk::DescriptorPoolCreateInfo pool_info;
            pool_info.setPoolSizes(pool_size);
            pool_info.setMaxSets(1);
            return device.createDescriptorPool(pool_info);
        });
        set_vertex_bindings([]() {
            return std::vector<vk::VertexInputBindingDescription>{
                vk::VertexInputBindingDescription()
                    .setBinding(0)
                    .setStride(sizeof(Vertex))
                    .setInputRate(vk::VertexInputRate::eVertex),
            };
        });
        add_shader(
            vk::ShaderStageFlagBits::eVertex, vertex_spv,
            vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
        );
        add_shader(
            vk::ShaderStageFlagBits::eFragment, fragment_spv,
            fragment_spv + sizeof(fragment_spv) / sizeof(uint32_t)
        );
        set_default_topology(vk::PrimitiveTopology::eTriangleList);
    }
};

void create_pipeline(
    ResMut<epix::render::vulkan2::RenderContext> context, Command cmd
) {
    if (!context) {
        return;
    }
    auto& device       = context->device;
    auto& swapchain    = context->primary_swapchain;
    auto& command_pool = context->command_pool;
    TestPipeline pipeline(device);
    pipeline.create();
    batch btch(pipeline, command_pool);
    staging_mesh mesh(device);
    cmd.insert_resource(std::move(pipeline));
    cmd.insert_resource(std::move(btch));
    cmd.insert_resource(std::move(mesh));
}

void extract_pipeline(
    ResMut<TestPipeline> pipeline,
    Command cmd,
    ResMut<batch> btch,
    ResMut<staging_mesh> mesh
) {
    if (!pipeline) {
        return;
    }
    cmd.share_resource(pipeline);
    cmd.share_resource(btch);
    cmd.share_resource(mesh);
}

void destroy_pipeline(
    ResMut<TestPipeline> pipeline, ResMut<batch> btch, ResMut<staging_mesh> mesh
) {
    if (!pipeline || !btch || !mesh) {
        return;
    }
    btch->destroy();
    mesh->destroy();
    pipeline->destroy();
}

void create_sampler(ResMut<VulkanResources> p_res_manager) {
    if (!p_res_manager) return;
    auto& res_manager = *p_res_manager;
    auto& device      = res_manager.device();
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
    ResMut<VulkanResources> p_res_manager,
    ResMut<epix::render::vulkan2::RenderContext> context
) {
    if (!p_res_manager || !context) {
        return;
    }
    auto& queue            = context->queue;
    auto& command_pool     = context->command_pool;
    auto& res_manager      = *p_res_manager;
    auto& device           = res_manager.device();
    auto image_create_info = vk::ImageCreateInfo()
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
    Res<VulkanResources> p_res_manager,
    ResMut<epix::render::vulkan2::RenderContext> context,
    ResMut<TestPipeline> pipeline,
    ResMut<batch> btch,
    ResMut<staging_mesh> mesh,
    Command cmd
) {
    if (!p_res_manager || !pipeline || !btch || !mesh || !context) {
        return;
    }
    ZoneScopedN("Test render");
    auto& res_manager = *p_res_manager;
    auto& device      = res_manager.device();
    auto& queue       = context->queue;
    auto& swapchain   = context->primary_swapchain;
    test_mesh ms;
    ms.set_16bit_indices();
    ms.emplace_vertex(glm::vec3{-0.5f, -0.5f, 0.0f}, glm::vec2{0.0f, 0.0f});
    ms.emplace_vertex(glm::vec3{0.5f, -0.5f, 0.0f}, glm::vec2{1.0f, 0.0f});
    ms.emplace_vertex(glm::vec3{0.5f, 0.5f, 0.0f}, glm::vec2{1.0f, 1.0f});
    ms.emplace_vertex(glm::vec3{-0.5f, 0.5f, 0.0f}, glm::vec2{0.0f, 1.0f});
    ms.emplace_index(0);
    ms.emplace_index(1);
    ms.emplace_index(2);
    ms.emplace_index(2);
    ms.emplace_index(3);
    ms.emplace_index(0);
    mesh->update(ms);
    btch->begin(
        [=](auto& device, auto& render_pass) {
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.setRenderPass(render_pass);
            framebuffer_info.setAttachments(swapchain.current_image_view());
            framebuffer_info.setWidth(swapchain.extent().width);
            framebuffer_info.setHeight(swapchain.extent().height);
            framebuffer_info.setLayers(1);
            return device.createFramebuffer(framebuffer_info);
        },
        swapchain.extent(),
        [=](auto& descriptor_sets) {
            descriptor_sets.resize(1);
            descriptor_sets[0] = res_manager.get_descriptor_set();
        }
    );
    PushConstants push_constants;
    push_constants.image_index =
        res_manager.image_view_index("test::rdvk::image1::view");
    push_constants.sampler_index =
        res_manager.sampler_index("test::rdvk::sampler1");
    btch->draw(*mesh, push_constants);
    btch->end(queue);
}

int main() {
    using namespace epix::app;
    using namespace epix::window;

    App app2 = App::create2();
    app2.add_plugin(WindowPlugin{});
    app2.get_plugin<WindowPlugin>()->primary_desc().set_vsync(false);
    app2.add_plugin(
        epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true)
    );
    app2.add_plugin(epix::input::InputPlugin{});
    app2.add_system(
        epix::Startup, create_pipeline, create_sampler, create_image_and_view
    );
    app2.add_system(epix::Extraction, extract_pipeline);
    app2.add_system(epix::Render, test_render);
    app2.add_system(epix::Exit, destroy_pipeline);
    app2.run();
}
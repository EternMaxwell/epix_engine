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

using test_mesh    = MultiDraw<Mesh<Vertex>, PushConstants>;
using staging_mesh = MultiDraw<StagingMesh<Mesh<Vertex>>, PushConstants>;
using gpu_mesh = MultiDraw<GPUMesh<StagingMesh<Mesh<Vertex>>>, PushConstants>;

struct TestPipeline : public PipelineBase {
    TestPipeline() : PipelineBase() {
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

struct TestPassBase : public PassBase {
   protected:
    TestPassBase(Device& device) : PassBase(device) {
        set_attachments(
            vk::AttachmentDescription()
                .setFormat(vk::Format::eR8G8B8A8Srgb)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setLoadOp(vk::AttachmentLoadOp::eLoad)
                .setStoreOp(vk::AttachmentStoreOp::eStore)
                .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
        );
        subpass_info(0)
            .set_colors(vk::AttachmentReference().setAttachment(0).setLayout(
                vk::ImageLayout::eColorAttachmentOptimal
            ))
            .set_bind_point(vk::PipelineBindPoint::eGraphics);
        create();
        add_pipeline(0, "test::rdvk::pipeline1", new TestPipeline());
    }

   public:
    static TestPassBase* create_new(Device& device) {
        return new TestPassBase(device);
    }
};

struct TestPass : public Pass {
   protected:
    TestPass(const PassBase* base, CommandPool& command_pool)
        : Pass(base, command_pool, [](Pass& pass, const PassBase& base) {
              pass.add_subpass(0);
              pass.subpass_add_pipeline(0, "test::rdvk::pipeline1");
          }) {}

   public:
    static TestPass* create_new(
        const TestPassBase* base, CommandPool& command_pool
    ) {
        return new TestPass(base, command_pool);
    }
};

void create_pass_base(Command cmd, Res<RenderContext> context) {
    if (!context) return;
    auto& device = context->device;
    cmd.add_resource(TestPassBase::create_new(device));
}

void create_pass(
    Command cmd, ResMut<RenderContext> context, Res<TestPassBase> base
) {
    if (!context) return;
    auto& device       = context->device;
    auto& command_pool = context->command_pool;
    cmd.add_resource(TestPass::create_new(base.get(), command_pool));
}

void create_meshes(Command cmd, Res<RenderContext> context) {
    if (!context) return;
    auto& device = context->device;
    staging_mesh mesh(device);
    cmd.insert_resource(mesh);
    gpu_mesh mesh2(device);
    cmd.insert_resource(mesh2);
}

void destroy_meshes(
    Command cmd, ResMut<staging_mesh> mesh, ResMut<gpu_mesh> mesh2
) {
    if (!mesh || !mesh2) return;
    mesh->destroy();
    mesh2->destroy();
}

void prepare_mesh(ResMut<staging_mesh> mesh, Res<VulkanResources> res) {
    if (!mesh || !res) return;
    PushConstants push_constants{
        res->image_view_index("test::rdvk::image1::view"),
        res->sampler_index("test::rdvk::sampler1")
    };
    if (push_constants.image_index == -1 ||
        push_constants.sampler_index == -1) {
        return;
    }
    test_mesh ms;
    ms.push_constant(push_constants);
    ms.emplace_vertex(glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f));
    ms.emplace_vertex(glm::vec3(0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f));
    ms.emplace_vertex(glm::vec3(0.5f, 0.5f, 0.0f), glm::vec2(1.0f, 1.0f));
    // ms.emplace_vertex(glm::vec3(-0.5f, 0.5f, 0.0f), glm::vec2(0.0f, 1.0f));
    ms.next_call();
    ms.emplace_vertex(glm::vec3(0.5f, 0.5f, 0.0f), glm::vec2(1.0f, 1.0f));
    ms.emplace_vertex(glm::vec3(-0.5f, 0.5f, 0.0f), glm::vec2(0.0f, 1.0f));
    ms.emplace_vertex(glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f));
    // ms.set_16bit_indices();
    // ms.emplace_index(0);
    // ms.emplace_index(1);
    // ms.emplace_index(2);
    // ms.emplace_index(2);
    // ms.emplace_index(3);
    // ms.emplace_index(0);
    ms.next_call();
    mesh->update(ms);
}

void extract_meshes(
    ResMut<staging_mesh> mesh, ResMut<gpu_mesh> mesh2, Command cmd
) {
    if (!mesh || !mesh2) return;
    cmd.share_resource(mesh);
    cmd.share_resource(mesh2);
}

void extract_pass(Command cmd, ResMut<TestPass> pass) {
    if (!pass) return;
    cmd.share_resource(pass);
}

void draw_mesh(
    Command cmd,
    ResMut<staging_mesh> mesh,
    Res<VulkanResources> res,
    ResMut<TestPass> pass,
    ResMut<gpu_mesh> gpu_mesh,
    ResMut<RenderContext> context
) {
    if (!mesh || !pass || !gpu_mesh) return;
    auto& pass_     = *pass;
    auto& mesh_     = *mesh;
    auto& gpu_mesh_ = *gpu_mesh;
    pass_.begin(
        [&](auto& device, auto& render_pass) {
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.setRenderPass(render_pass);
            framebuffer_info.setAttachments(
                context->primary_swapchain.current_image_view()
            );
            framebuffer_info.setWidth(context->primary_swapchain.extent().width
            );
            framebuffer_info.setHeight(
                context->primary_swapchain.extent().height
            );
            framebuffer_info.setLayers(1);
            return device.createFramebuffer(framebuffer_info);
        },
        context->primary_swapchain.extent()
    );
    pass_.update_mesh(gpu_mesh_, mesh_);
    auto subpass = pass_.next_subpass();
    subpass.activate_pipeline(
        0,
        [&](auto& viewports, auto& scissors) {
            viewports.resize(1);
            viewports[0].setWidth(context->primary_swapchain.extent().width);
            viewports[0].setHeight(context->primary_swapchain.extent().height);
            viewports[0].setMinDepth(0.0f);
            viewports[0].setMaxDepth(1.0f);
            scissors.resize(1);
            scissors[0].setExtent(context->primary_swapchain.extent());
            scissors[0].setOffset({0, 0});
        },
        [&](auto& device, auto& descriptor_sets) {
            descriptor_sets.resize(1);
            descriptor_sets[0] = res->get_descriptor_set();
        }
    );
    subpass.draw(gpu_mesh_);
    pass_.end();
    pass_.submit(context->queue);
}

void destroy_pass_base(Command cmd, ResMut<TestPassBase> pass) {
    if (!pass) return;
    pass->destroy();
}

void destroy_pass(Command cmd, ResMut<TestPass> pass) {
    if (!pass) return;
    pass->destroy();
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
        epix::Startup, epix::bundle(create_sampler, create_image_and_view)
    );
    app2.add_system(epix::Startup, epix::chain(create_pass_base, create_pass));
    app2.add_system(epix::Startup, epix::into(create_meshes));
    app2.add_system(
        epix::Extraction,
        epix::bundle(extract_pass, prepare_mesh, extract_meshes)
    );
    app2.add_system(epix::Render, epix::into(draw_mesh));
    app2.add_system(epix::Exit, epix::chain(destroy_pass, destroy_pass_base));
    app2.add_system(epix::Exit, epix::into(destroy_meshes));
    app2.run();
}
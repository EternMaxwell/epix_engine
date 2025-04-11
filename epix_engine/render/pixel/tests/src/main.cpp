#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/render/pixel.h>
#include <epix/window.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace epix;

void create_camera_uniform_buffer(
    ResMut<epix::render::vulkan2::VulkanResources> res_manager, Command cmd
) {
    using namespace epix::render::vulkan2::backend;
    auto& device = res_manager->device();
    auto buffer  = device.createBuffer(
        vk::BufferCreateInfo()
            .setSize(sizeof(glm::mat4) * 2)
            .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
    );
    auto* data = (glm::mat4*)buffer.map();
    data[0]    = glm::mat4(1.0f);
    data[1]    = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
    buffer.unmap();
    res_manager->add_buffer("camera_uniform_buffer", buffer);
}

struct TestMesh : epix::render::pixel::vulkan2::PixelDrawMesh {};
struct TestStagingMesh : epix::render::pixel::vulkan2::PixelDrawStagingMesh {};
struct TestGPUMesh : epix::render::pixel::vulkan2::PixelDrawGPUMesh {};

struct TestPassBase : public epix::render::vulkan2::PassBase {
   protected:
    TestPassBase(epix::render::vulkan2::backend::Device& device)
        : PassBase(device) {
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
        add_pipeline(
            0, "test::pixel::pipeline",
            epix::render::pixel::vulkan2::PixelPipeline::create()
        );
    }

   public:
    static TestPassBase* create_new(
        epix::render::vulkan2::backend::Device& device
    ) {
        return new TestPassBase(device);
    }
};

using namespace epix::render::vulkan2;

struct TestPass : public epix::render::vulkan2::Pass {
   protected:
    TestPass(
        const TestPassBase* base,
        epix::render::vulkan2::backend::CommandPool& command_pool
    )
        : Pass(base, command_pool, [](Pass& pass, const PassBase& base) {
              using namespace epix::render::vulkan2::backend;
              pass.add_subpass(0);
              auto create_desc_set =
                  [](Device& device, const DescriptorPool& pool,
                     const std::vector<DescriptorSetLayout>& layouts) {
                      return device.allocateDescriptorSets(
                          vk::DescriptorSetAllocateInfo()
                              .setDescriptorPool(pool)
                              .setSetLayouts(layouts[0])
                      );
                  };
              auto destroy_desc_set = [](Device& device,
                                         const DescriptorPool& pool,
                                         std::vector<DescriptorSet>& sets) {
                  device.freeDescriptorSets(pool, sets[0]);
              };
              pass.subpass_add_pipeline(
                  0, "test::pixel::pipeline", create_desc_set, destroy_desc_set
              );
          }) {}

   public:
    static TestPass* create_new(
        const TestPassBase* base,
        epix::render::vulkan2::backend::CommandPool& command_pool
    ) {
        return new TestPass(base, command_pool);
    }
};

void create_pass_base(
    Command cmd, Res<epix::render::vulkan2::RenderContext> context
) {
    if (!context) {
        return;
    }
    auto& device = context->device;
    cmd.add_resource(TestPassBase::create_new(device));
}

void create_pass(
    Command cmd,
    ResMut<epix::render::vulkan2::RenderContext> context,
    Res<TestPassBase> base
) {
    if (!context) {
        return;
    }
    auto& device       = context->device;
    auto& command_pool = context->command_pool;
    cmd.add_resource(TestPass::create_new(base.get(), command_pool));
}

void destroy_pass_base(Command cmd, ResMut<TestPassBase> pass) {
    if (!pass) {
        return;
    }
    pass->destroy();
}

void destroy_pass(Command cmd, ResMut<TestPass> pass) {
    if (!pass) {
        return;
    }
    pass->destroy();
}

void create_meshes(
    Command cmd, Res<epix::render::vulkan2::RenderContext> context
) {
    if (!context) {
        return;
    }
    auto& device = context->device;
    TestStagingMesh mesh(device);
    cmd.insert_resource(mesh);
    TestGPUMesh mesh2(device);
    cmd.insert_resource(mesh2);
}

void destroy_meshes(
    Command cmd, ResMut<TestStagingMesh> mesh, ResMut<TestGPUMesh> mesh2
) {
    if (!mesh || !mesh2) {
        return;
    }
    mesh->destroy();
    mesh2->destroy();
}

void extract_meshes(
    Extract<ResMut<TestStagingMesh>> mesh,
    Extract<ResMut<TestGPUMesh>> mesh2,
    Command cmd
) {
    if (!mesh || !mesh2) {
        return;
    }
    cmd.share_resource(mesh);
    cmd.share_resource(mesh2);
}

void extract_pass(Extract<ResMut<TestPass>> pass, Command cmd) {
    if (!pass) {
        return;
    }
    cmd.share_resource(pass);
}

void prepare_mesh(Extract<ResMut<TestStagingMesh>> mesh) {
    if (!mesh) {
        return;
    }
    ZoneScopedN("Prepare mesh");
    auto& mesh_data = *mesh;
    TestMesh ms;
    ms.emplace_constant(1.0f);
    ms.draw_pixel(glm::vec2(0.0f), glm::vec4(1.0f));
    ms.next_call();
    mesh_data.update(ms);
}

void draw_mesh(
    Res<TestStagingMesh> staging_mesh,
    ResMut<TestGPUMesh> mesh,
    ResMut<TestPass> pass,
    ResMut<epix::render::vulkan2::RenderContext> context,
    Res<epix::render::vulkan2::VulkanResources> res_manager
) {
    if (!staging_mesh || !mesh || !pass || !context) {
        return;
    }
    auto& queue         = context->queue;
    auto& pass_         = *pass;
    auto& mesh_         = *mesh;
    auto& staging_mesh_ = *staging_mesh;
    pass_.begin(
        [&](auto& device, auto& renderpass) {
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.setRenderPass(renderpass);
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
    pass_.update_mesh(mesh_, staging_mesh_);
    auto& subpass = pass_.next_subpass();
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
            vk::DescriptorBufferInfo buffer_info;
            buffer_info.setBuffer(
                res_manager->get_buffer("camera_uniform_buffer")
            );
            buffer_info.setOffset(0);
            buffer_info.setRange(sizeof(glm::mat4) * 2);
            vk::WriteDescriptorSet descriptor_writes =
                vk::WriteDescriptorSet()
                    .setDstSet(descriptor_sets[0])
                    .setDstBinding(0)
                    .setDstArrayElement(0)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setDescriptorCount(1)
                    .setPBufferInfo(&buffer_info);
            device.updateDescriptorSets(descriptor_writes, {});
        }
    );
    subpass.draw(mesh_);
    pass_.end();
    pass_.submit(queue);
}

int main() {
    epix::App app = epix::App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_desc().set_vsync(false
    );
    app.add_plugin(epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true
    ));
    app.add_plugin(epix::input::InputPlugin{});
    app.add_system(epix::Startup, into(create_camera_uniform_buffer));
    app.add_system(
        epix::Startup,
        into(create_pass_base, create_pass, create_meshes).chain()
    );
    app.add_system(
        epix::Extraction, into(prepare_mesh, extract_meshes, extract_pass)
    );
    app.add_system(epix::Render, into(draw_mesh));
    app.add_system(
        epix::Exit,
        into(destroy_pass, destroy_pass_base, destroy_meshes).chain()
    );
    app.run();
    return 0;
}
#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/render/debug.h>
#include <epix/window.h>

using namespace epix;

EPIX_SYSTEM(
    create_camera_uniform_buffer,
    (ResMut<epix::render::vulkan2::VulkanResources> res_manager, Command cmd) {
        using namespace epix::render::vulkan2::backend;
        auto& device = res_manager->device();
        auto buffer  = device.createBuffer(
            vk::BufferCreateInfo()
                .setSize(sizeof(glm::mat4) * 2)
                .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
                .setSharingMode(vk::SharingMode::eExclusive),
            AllocationCreateInfo()
                .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
                .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                 )
        );
        auto* data = (glm::mat4*)buffer.map();
        data[0]    = glm::mat4(1.0f);
        data[1]    = glm::mat4(1.0f);
        buffer.unmap();
        res_manager->add_buffer("camera_uniform_buffer", buffer);
    }
)

struct TestMesh : epix::render::debug::vulkan2::DebugDrawMesh {};
struct TestStagingMesh : epix::render::debug::vulkan2::DebugDrawStagingMesh {};
struct TestGPUMesh : epix::render::debug::vulkan2::DebugDrawGPUMesh {};

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
            0, "test::debug::pipeline::point",
            epix::render::debug::vulkan2::DebugPipelines::point_pipeline()
        );
        add_pipeline(
            0, "test::debug::pipeline::line",
            epix::render::debug::vulkan2::DebugPipelines::line_pipeline()
        );
        add_pipeline(
            0, "test::debug::pipeline::triangle",
            epix::render::debug::vulkan2::DebugPipelines::triangle_pipeline()
        );
    }

   public:
    static TestPassBase* create_new(
        epix::render::vulkan2::backend::Device& device
    ) {
        return new TestPassBase(device);
    }
};

struct TestPass : public epix::render::vulkan2::Pass {
   protected:
    TestPass(
        const epix::render::vulkan2::PassBase* base,
        epix::render::vulkan2::backend::CommandPool& command_pool
    )
        : Pass(
              base,
              command_pool,
              [](epix::render::vulkan2::Pass& pass,
                 const epix::render::vulkan2::PassBase& base) {
                  using namespace epix::render::vulkan2::backend;
                  pass.add_subpass(0);
                  auto create_desc_set =
                      [](Device& device, const DescriptorPool& pool,
                         const std::vector<DescriptorSetLayout>& layouts) {
                          std::vector<DescriptorSet> sets;
                          sets.resize(1);
                          sets[0] = device.allocateDescriptorSets(
                              vk::DescriptorSetAllocateInfo()
                                  .setDescriptorPool(pool)
                                  .setSetLayouts(layouts[0])
                          )[0];
                          return sets;
                      };
                  auto destroy_desc_set = [](Device& device,
                                             const DescriptorPool& pool,
                                             std::vector<DescriptorSet>& sets) {
                      device.freeDescriptorSets(pool, sets[0]);
                  };
                  pass.subpass_add_pipeline(
                      0, "test::debug::pipeline::point", create_desc_set,
                      destroy_desc_set
                  );
                  pass.subpass_add_pipeline(
                      0, "test::debug::pipeline::line", create_desc_set,
                      destroy_desc_set
                  );
                  pass.subpass_add_pipeline(
                      0, "test::debug::pipeline::triangle", create_desc_set,
                      destroy_desc_set
                  );
              }
          ) {}

   public:
    static TestPass* create_new(
        const TestPassBase* base,
        epix::render::vulkan2::backend::CommandPool& command_pool
    ) {
        return new TestPass(base, command_pool);
    }
};

EPIX_SYSTEM(
    create_pass_base,
    (Command cmd, Res<epix::render::vulkan2::RenderContext> context) {
        if (!context) {
            return;
        }
        auto& device = context->device;
        cmd.add_resource(TestPassBase::create_new(device));
    }
)
EPIX_SYSTEM(
    create_pass,
    (Command cmd,
     ResMut<epix::render::vulkan2::RenderContext> context,
     Res<TestPassBase> base) {
        if (!context) {
            return;
        }
        auto& device       = context->device;
        auto& command_pool = context->command_pool;
        cmd.add_resource(TestPass::create_new(base.get(), command_pool));
    }
)
EPIX_SYSTEM(
    create_meshes,
    (Command cmd, Res<epix::render::vulkan2::RenderContext> context) {
        if (!context) {
            return;
        }
        auto& device = context->device;
        TestStagingMesh mesh(device);
        cmd.insert_resource(mesh);
        TestGPUMesh mesh2(device);
        cmd.insert_resource(mesh2);
    }
)
EPIX_SYSTEM(
    prepare_mesh,
    (ResMut<TestStagingMesh> mesh) {
        if (!mesh) {
            return;
        }
        ZoneScopedN("Prepare mesh");
        auto& mesh_data = *mesh;
        TestMesh ms;
        ms.emplace_constant(1.0f);
        ms.draw_point({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        ms.draw_point({0.1f, 0.1f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f});
        ms.draw_point({-0.1f, 0.1f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
        ms.next_call();
        mesh_data.update(ms);
    }
)

EPIX_SYSTEM(
    destroy_meshes,
    (Command cmd, ResMut<TestStagingMesh> mesh, ResMut<TestGPUMesh> mesh2) {
        if (!mesh || !mesh2) {
            return;
        }
        mesh->destroy();
        mesh2->destroy();
    }
)

EPIX_SYSTEM(
    extract_meshes,
    (ResMut<TestStagingMesh> mesh, ResMut<TestGPUMesh> mesh2, Command cmd) {
        if (!mesh || !mesh2) {
            return;
        }
        cmd.share_resource(mesh);
        cmd.share_resource(mesh2);
    }
)
EPIX_SYSTEM(
    draw_mesh,
    (Res<TestStagingMesh> staging_mesh,
     ResMut<TestGPUMesh> mesh,
     ResMut<TestPass> pass,
     ResMut<epix::render::vulkan2::RenderContext> context,
     Res<epix::render::vulkan2::VulkanResources> res_manager) {
        if (!mesh || !pass || !context || !staging_mesh || !res_manager) {
            return;
        }
        auto& queue = context->queue;
        pass->begin(
            [&](auto& device, auto& render_pass) {
                vk::FramebufferCreateInfo framebuffer_info;
                framebuffer_info.setRenderPass(render_pass);
                framebuffer_info.setAttachments(
                    context->primary_swapchain.current_image_view()
                );
                framebuffer_info.setWidth(
                    context->primary_swapchain.extent().width
                );
                framebuffer_info.setHeight(
                    context->primary_swapchain.extent().height
                );
                framebuffer_info.setLayers(1);
                return device.createFramebuffer(framebuffer_info);
            },
            context->primary_swapchain.extent()
        );
        pass->update_mesh(*mesh, *staging_mesh);
        auto& subpass = pass->next_subpass();
        subpass.activate_pipeline(
            0,
            [&](auto& viewports, auto& scissors) {
                viewports.resize(1);
                viewports[0].setWidth(context->primary_swapchain.extent().width
                );
                viewports[0].setHeight(
                    context->primary_swapchain.extent().height
                );
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
        subpass.draw(*mesh, glm::mat4(1.0f));
        pass->end();
        pass->submit(queue);
    }
)
EPIX_SYSTEM(
    extract_pass,
    (Command cmd, ResMut<TestPass> pass) {
        if (!pass) {
            return;
        }
        cmd.share_resource(pass);
    }
)
EPIX_SYSTEM(
    destroy_pass_base,
    (Command cmd, ResMut<TestPassBase> pass) {
        if (!pass) {
            return;
        }
        pass->destroy();
    }
)
EPIX_SYSTEM(
    destroy_pass,
    (Command cmd, ResMut<TestPass> pass) {
        if (!pass) {
            return;
        }
        pass->destroy();
    }
)

int main() {
    epix::App app = epix::App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_desc().set_vsync(false
    );
    app.add_plugin(epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true
    ));
    app.add_plugin(epix::input::InputPlugin{});
    app.add_system(
        epix::Startup, chain(
                           create_camera_uniform_buffer, create_pass_base,
                           create_pass, create_meshes
                       )
    );
    app.add_system(
        epix::Extraction, bundle(prepare_mesh, extract_meshes, extract_pass)
    );
    app.add_system(epix::Render, draw_mesh);
    app.add_system(
        epix::Exit, chain(destroy_pass, destroy_pass_base, destroy_meshes)
    );
    app.run();
    return 0;
}
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

struct ImmediateCtx : epix::render::pixel::vulkan2::PixelBatch {
    ImmediateCtx(
        epix::render::pixel::vulkan2::PixelPipeline& pipeline,
        vk::CommandPool& pool
    )
        : PixelBatch(pipeline, pool) {}
};

struct TestMesh : epix::render::pixel::vulkan2::PixelMesh {};
struct TestStagingMesh : epix::render::pixel::vulkan2::PixelStagingMesh {};

void create_context_and_mesh(
    ResMut<epix::render::vulkan2::RenderContext> context,
    ResMut<epix::render::pixel::vulkan2::PixelPipeline> pipeline,
    Command cmd
) {
    ImmediateCtx immediate_context(*pipeline, context->command_pool);
    TestStagingMesh mesh(context->device);
    cmd.insert_resource(std::move(immediate_context));
    cmd.insert_resource(std::move(mesh));
}

void extract_context_and_mesh(
    ResMut<ImmediateCtx> immediate_context,
    ResMut<TestStagingMesh> mesh,
    Command cmd
) {
    if (!immediate_context || !mesh) {
        return;
    }
    ZoneScopedN("Extract immediate ctx and mesh");
    cmd.share_resource(immediate_context);
    cmd.share_resource(mesh);
}

void prepare_mesh(ResMut<TestStagingMesh> mesh) {
    if (!mesh) {
        return;
    }
    ZoneScopedN("Prepare mesh");
    auto& mesh_data = *mesh;
    TestMesh ms;
    ms.draw_pixel(glm::vec2(0.0f), glm::vec4(1.0f));
    mesh_data.update(ms);
}

void draw_mesh(
    ResMut<epix::render::vulkan2::RenderContext> context,
    Res<epix::render::vulkan2::VulkanResources> res_manager,
    ResMut<ImmediateCtx> immediate_context,
    ResMut<TestStagingMesh> mesh,
    Command cmd
) {
    if (!immediate_context || !mesh || !context || !res_manager) {
        return;
    }
    ZoneScopedN("Draw mesh");
    immediate_context->begin(
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
        context->primary_swapchain.extent(),
        [&](auto& device, auto& descriptor_sets) {
            vk::WriteDescriptorSet write_descriptor_set;
            write_descriptor_set.setDstSet(descriptor_sets[0]);
            write_descriptor_set.setDstBinding(0);
            write_descriptor_set.setDescriptorType(
                vk::DescriptorType::eUniformBuffer
            );
            write_descriptor_set.setDescriptorCount(1);
            vk::DescriptorBufferInfo buffer_info;
            buffer_info.setBuffer(
                res_manager->get_buffer("camera_uniform_buffer")
            );
            buffer_info.setOffset(0);
            buffer_info.setRange(sizeof(glm::mat4) * 2);
            write_descriptor_set.setBufferInfo(buffer_info);
            device.updateDescriptorSets(write_descriptor_set, {});
        }
    );
    immediate_context->draw(*mesh, glm::mat4(1.0f));
    immediate_context->end(context->queue);
}

void destroy_context_and_mesh(
    ResMut<ImmediateCtx> immediate_context,
    ResMut<TestStagingMesh> mesh,
    Command cmd
) {
    if (!immediate_context || !mesh) {
        return;
    }
    immediate_context->destroy();
    mesh->destroy();
}

int main() {
    epix::App app = epix::App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_desc().set_vsync(false
    );
    app.add_plugin(epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true
    ));
    app.add_plugin(epix::input::InputPlugin{});
    app.add_plugin(epix::render::pixel::PixelRenderPlugin{});
    app.add_system(epix::Startup, create_camera_uniform_buffer);
    app.add_system(epix::Startup, create_context_and_mesh);
    app.add_system(epix::Extraction, extract_context_and_mesh);
    app.add_system(epix::Extraction, prepare_mesh);
    app.add_system(epix::Render, draw_mesh);
    app.add_system(epix::Exit, destroy_context_and_mesh);
    app.run();
    return 0;
}
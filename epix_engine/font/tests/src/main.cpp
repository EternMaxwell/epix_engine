#include <epix/font.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/window.h>

using namespace epix::font::vulkan2;
using namespace epix::render::vulkan2;

using namespace epix;

void create_batch_gpu_mesh(
    Command cmd, ResMut<TextPipeline> pipeline, ResMut<RenderContext> context
) {
    TextBatch batch{*pipeline, context->command_pool};
    TextStagingMesh staging_mesh{context->device};
    cmd.insert_resource(std::move(batch));
    cmd.insert_resource(std::move(staging_mesh));
}

void extract_batch_gpu_mesh(
    ResMut<TextBatch> batch, ResMut<TextStagingMesh> staging_mesh, Command cmd
) {
    if (!batch || !staging_mesh) {
        return;
    }
    cmd.share_resource(batch);
    cmd.share_resource(staging_mesh);
}

void create_uniform_buffer(
    ResMut<VulkanResources> res_manager, Res<RenderContext> context
) {
    if (!res_manager || !context) {
        return;
    }
    auto& device = context->device;
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
    data[1]    = glm::scale(
        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f)),
        glm::vec3(9.0f, 16.0f, 1.0f)
    );
    buffer.unmap();
    res_manager->add_buffer("uniform_buffer", buffer);
}

void prepare_mesh(
    ResMut<TextStagingMesh> staging_mesh,
    Res<FontAtlas> font_atlas,
    Res<VulkanResources> res_manager
) {
    if (!staging_mesh) {
        return;
    }
    auto& mesh_data = *staging_mesh;
    TextMesh ms;
    Text text;
    text.font.font_identifier = "default";
    text.text                 = L"Hello, World!";
    text.height               = 0.005f;
    text.center               = {0.5f, 0.5f};
    ms.draw_text(text, {0.0f, 0.0f}, font_atlas.get(), res_manager.get());
    mesh_data.update(ms);
}

void draw_mesh(
    ResMut<RenderContext> context,
    Res<VulkanResources> res_manager,
    ResMut<TextBatch> batch,
    Res<TextStagingMesh> staging_mesh,
    Command cmd
) {
    if (!batch || !staging_mesh || !context || !res_manager) {
        return;
    }
    auto& batch_data = *batch;
    auto& mesh_data  = *staging_mesh;
    auto& swapchain  = context->primary_swapchain;
    batch_data.begin(
        [=](auto& device, auto& render_pass) {
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.setRenderPass(render_pass);
            framebuffer_info.setAttachments(swapchain.current_image_view());
            framebuffer_info.setWidth(swapchain.extent().width);
            framebuffer_info.setHeight(swapchain.extent().height);
            framebuffer_info.setLayers(1);
            return device.createFramebuffer(framebuffer_info);
        },
        context->primary_swapchain.extent(),
        [&](auto& device, auto& descriptor_sets) {
            descriptor_sets.resize(2);
            vk::WriteDescriptorSet descriptor_write;
            vk::DescriptorBufferInfo buffer_info;
            buffer_info.setBuffer(res_manager->get_buffer("uniform_buffer"));
            buffer_info.setOffset(0);
            buffer_info.setRange(sizeof(glm::mat4) * 2);
            descriptor_write.setDstSet(descriptor_sets[0]);
            descriptor_write.setDstBinding(0);
            descriptor_write.setDstArrayElement(0);
            descriptor_write.setDescriptorType(
                vk::DescriptorType::eUniformBuffer
            );
            descriptor_write.setDescriptorCount(1);
            descriptor_write.setPBufferInfo(&buffer_info);
            auto descriptor_writes = {descriptor_write};
            device.updateDescriptorSets(descriptor_writes, nullptr);
            descriptor_sets[1] = res_manager->get_descriptor_set();
        }
    );
    batch_data.draw(mesh_data, glm::mat4(1.0f));
    batch_data.end(context->queue);
}

void destory_batch_gpu_mesh(
    ResMut<TextBatch> batch, ResMut<TextStagingMesh> staging_mesh
) {
    if (!batch || !staging_mesh) {
        return;
    }
    batch->destroy();
    staging_mesh->destroy();
}

int main() {
    App app = App::create2();
    app.add_plugin(window::WindowPlugin{});
    app.get_plugin<window::WindowPlugin>()->primary_desc().set_vsync(false);
    app.add_plugin(font::FontPlugin{})
        .add_plugin(VulkanPlugin{}.set_debug_callback(true))
        .add_plugin(input::InputPlugin{})
        .add_system(Startup, create_batch_gpu_mesh, create_uniform_buffer);
    app.add_system(Extraction, prepare_mesh);
    app.add_system(Extraction, extract_batch_gpu_mesh);
    app.add_system(Render, draw_mesh);
    app.add_system(Exit, destory_batch_gpu_mesh);
    app.run();
}
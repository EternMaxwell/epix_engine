#include <epix/font.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/window.h>

using namespace epix::font::vulkan2;
using namespace epix::render::vulkan2;

using namespace epix;

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

int main() {
    App app = App::create2();
    app.add_plugin(window::WindowPlugin{});
    app.get_plugin<window::WindowPlugin>()->primary_desc().set_vsync(false);
    app.add_plugin(font::FontPlugin{})
        .add_plugin(VulkanPlugin{}.set_debug_callback(true))
        .add_plugin(input::InputPlugin{})
        .add_system(Startup,create_uniform_buffer);
    app.add_system(Extraction, prepare_mesh);
    app.run();
}
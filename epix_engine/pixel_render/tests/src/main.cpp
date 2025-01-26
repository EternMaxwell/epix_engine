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

struct TestMesh : epix::render::pixel::vulkan2::PixelMesh {};
struct TestStagingMesh : epix::render::pixel::vulkan2::PixelStagingMesh {};

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
    app.add_system(epix::Extraction, prepare_mesh);
    app.run();
    return 0;
}
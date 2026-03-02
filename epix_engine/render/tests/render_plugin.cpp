#include <spdlog/spdlog.h>

#include <stacktrace>

import epix.core;
import epix.input;
import epix.window;
import epix.render;
import epix.glfw.core;
import epix.glfw.render;

import std;

using namespace core;

void test_system(Res<render::window::ExtractedWindows> windows, Res<wgpu::Device> device, Res<wgpu::Queue> queue) {
    auto encoder = device->createCommandEncoder();
    for (auto&& [entity, window] : windows->windows) {
        auto render_pass = encoder.beginRenderPass(
            wgpu::RenderPassDescriptor().setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                                            .setView(window.swapchain_texture_view)
                                                                            .setLoadOp(wgpu::LoadOp::eClear)
                                                                            .setClearValue({0.3f, 0.3f, 0.3f, 1.0f})
                                                                            .setStoreOp(wgpu::StoreOp::eStore)
                                                                            .setDepthSlice(~0u)}));
        render_pass.end();
    }
    auto buffer = encoder.finish();
    queue->submit(buffer);
}

int main() {
    App app = App::create();

    app.add_plugins(window::WindowPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GlfwRenderPlugin{})
        .add_plugins(render::RenderPlugin{});
    // auto& render_app = app.sub_app_mut(render::Render);
    // render_app.add_systems(render::Render, into(test_system).set_name("test system").in_set(render::RenderSet::Render));

    app.run();
}
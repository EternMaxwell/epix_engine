#include <epix/core.hpp>
#include <epix/core_graph.hpp>
#include <epix/glfw.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>

int main() {
    epix::App app = epix::App::create();

    app.add_plugins(epix::window::WindowPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::transform::TransformPlugin{})
        .add_plugins(epix::render::RenderPlugin{})
        .add_plugins(epix::core_graph::CoreGraphPlugin{});
    app.world_mut().spawn(epix::render::core_2d::Camera2DBundle{});

    app.run();
}
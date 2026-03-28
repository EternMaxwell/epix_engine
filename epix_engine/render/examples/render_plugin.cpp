#include <spdlog/spdlog.h>

#include <stacktrace>

import epix.core;
import epix.input;
import epix.window;
import epix.transform;
import epix.render;
import epix.glfw.core;
import epix.glfw.render;

import std;

using namespace core;

constexpr struct Test {
} test_graph;

int main() {
    App app = App::create();

    app.add_plugins(window::WindowPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{});
    app.add_systems(Startup,
                    into([](Commands cmd) { cmd.spawn(render::camera::CameraBundle::with_render_graph(test_graph)); }));
    // auto& render_app = app.sub_app_mut(render::Render);
    // render_app.add_systems(render::Render, into(test_system).set_name("test
    // system").in_set(render::RenderSet::Render));

    app.run();
}
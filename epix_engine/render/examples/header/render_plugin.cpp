#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <stacktrace>
using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

constexpr struct Test {
} test_graph;

int main() {
    App app = App::create();

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{})
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
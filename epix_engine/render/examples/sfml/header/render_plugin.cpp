#include <spdlog/spdlog.h>

#include <epix/ecs.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/sfml/core.hpp>
#include <epix/sfml/render.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <stacktrace>
using namespace epix::ecs;
using namespace epix::app;
using namespace epix;

constexpr struct Test {
} test_graph;

int main() {
    App app = App::create();

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_plugins(sfml::SFMLPlugin{})
        .add_plugins(sfml::SFMLRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{});
    app.add_systems(Startup,
                    into([](Commands cmd) { cmd.spawn(render::camera::CameraBundle::with_render_graph(test_graph)); }));

    app.run();
}

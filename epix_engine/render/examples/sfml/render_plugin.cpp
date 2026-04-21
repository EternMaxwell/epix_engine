#include <spdlog/spdlog.h>

#include <stacktrace>

import epix.core;
import epix.input;
import epix.window;
import epix.transform;
import epix.render;
import epix.sfml.core;
import epix.sfml.render;
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix::core;
using namespace epix;

constexpr struct Test {
} test_graph;

int main() {
    App app = App::create();

    app.add_plugins(window::WindowPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_plugins(sfml::SFMLPlugin{})
        .add_plugins(sfml::SFMLRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{});
    app.add_systems(Startup,
                    into([](Commands cmd) { cmd.spawn(render::camera::CameraBundle::with_render_graph(test_graph)); }));

    app.run();
}

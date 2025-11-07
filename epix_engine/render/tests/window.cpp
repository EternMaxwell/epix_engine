#include <epix/core.hpp>
#include <epix/glfw.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>

int main() {
    epix::App app = epix::App::create();

    app.add_plugins(epix::window::WindowPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::render::RenderPlugin{});

    app.run();
}
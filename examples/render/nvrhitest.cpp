#include <epix/app.h>
#include <epix/render.h>
#include <epix/render/pipeline.h>
#include <epix/window.h>

using namespace epix;

int main() {
    App app = App::create();
    app.add_plugins(window::WindowPlugin{});
    app.add_plugins(glfw::GLFWPlugin{});
    app.add_plugins(input::InputPlugin{});
    app.add_plugins(render::RenderPlugin{}.enable_validation(true));
    app.run();
    return 0;
}
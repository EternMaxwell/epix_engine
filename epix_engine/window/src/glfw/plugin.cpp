#include "epix/glfw/glfw.hpp"

using namespace epix::glfw;
using namespace epix;

void GLFWPlugin::build(App& app) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<GLFWwindows>();
    app.add_events<SetCustomCursor>().add_events<SetClipboardString>().set_runner(std::make_unique<GLFWRunner>(app));
}
module;

#include <GLFW/glfw3.h>

module epix.glfw.core;

using namespace glfw;
using namespace core;

void GLFWPlugin::build(App& app) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<GLFWwindows>();
    app.add_events<SetClipboardString>().set_runner(std::make_unique<GLFWRunner>(app));
}
#include "epix/window.h"

using namespace epix::glfw;
using namespace epix;

EPIX_API void GLFWPlugin::build(App& app) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    app.insert_resource(Clipboard{})
        .init_resource<GLFWwindows>()
        .add_events<SetCustomCursor>()
        .add_events<SetClipboardString>()
        .set_runner(std::make_unique<GLFWRunner>());
}
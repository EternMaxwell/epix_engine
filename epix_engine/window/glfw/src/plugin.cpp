module;

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include <cstdlib>

module epix.glfw.core;

using namespace epix::glfw;
using namespace epix::core;

namespace {
const char* glfw_platform_name(int platform) {
    switch (platform) {
        case GLFW_PLATFORM_WIN32:
            return "Win32";
        case GLFW_PLATFORM_COCOA:
            return "Cocoa";
        case GLFW_PLATFORM_X11:
            return "X11";
        case GLFW_PLATFORM_WAYLAND:
            return "Wayland";
        case GLFW_PLATFORM_NULL:
            return "Null";
        default:
            return "Unknown";
    }
}

bool should_prefer_x11_on_wslg() {
    const char* wsl_distro      = std::getenv("WSL_DISTRO_NAME");
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const char* display         = std::getenv("DISPLAY");
    return wsl_distro && wayland_display && display && glfwPlatformSupported(GLFW_PLATFORM_X11) == GLFW_TRUE;
}
}  // namespace

void GLFWPlugin::build(App& app) {
    spdlog::debug("[glfw] Initializing GLFW.");
    if (should_prefer_x11_on_wslg()) {
        spdlog::debug("[glfw] WSLg mixed session detected; preferring X11 for decorated GLFW windows.");
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    spdlog::info("[glfw] GLFW initialized successfully on platform {}.", glfw_platform_name(glfwGetPlatform()));
    app.add_plugins(image::ImagePlugin{});
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<GLFWwindows>();
    app.add_events<SetClipboardString>().set_runner(std::make_unique<GLFWRunner>(app));
}

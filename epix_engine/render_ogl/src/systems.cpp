#include "epix/render_ogl/systems.h"

using namespace epix::render::ogl;

EPIX_API void systems::clear_color(Query<Get<window::Window>> query) {
    for (auto [window] : query.iter()) {
        if (window.get_handle() == nullptr) return;
        if (glfwGetCurrentContext() != window.get_handle())
            glfwMakeContextCurrent(window.get_handle());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

EPIX_API void systems::update_viewport(Query<Get<window::Window>> query) {
    for (auto [window] : query.iter()) {
        if (window.get_handle() == nullptr) return;
        if (glfwGetCurrentContext() != window.get_handle())
            glfwMakeContextCurrent(window.get_handle());
        glViewport(0, 0, window.get_size().width, window.get_size().height);
    }
}

EPIX_API void systems::context_creation(
    Command cmd,
    Query<Get<Entity, window::Window>, Without<systems::ContextCreated>> query
) {
    for (auto [entity, window] : query.iter()) {
        if (window.get_handle() == nullptr) return;
        if (glfwGetCurrentContext() != window.get_handle())
            glfwMakeContextCurrent(window.get_handle());
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            spdlog::error("Failed to initialize OpenGL context");
            throw std::runtime_error("Failed to initialize OpenGL context");
        }
        cmd.entity(entity).emplace(systems::ContextCreated{});
#ifndef NDEBUG
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(
            [](GLenum source, GLenum type, GLuint id, GLenum severity,
               GLsizei length, const GLchar* message, const void* userParam) {
                if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
                    spdlog::debug(
                        "OpenGL debug message: source: {}, type: {}, id: {}, "
                        "severity: {}, message: {}",
                        source, type, id, severity, message
                    );
                    return;
                }
                spdlog::warn(
                    "OpenGL debug message: source: {}, type: {}, id: {}, "
                    "severity: {}, message: {}",
                    source, type, id, severity, message
                );
            },
            nullptr
        );
#endif
    }
}

EPIX_API void systems::swap_buffers(Query<Get<window::Window>> query) {
    for (auto [window] : query.iter()) {
        if (window.get_handle() == nullptr) return;
        if (glfwGetCurrentContext() != window.get_handle())
            glfwMakeContextCurrent(window.get_handle());
        glfwSwapBuffers(window.get_handle());
    }
}

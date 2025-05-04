#include "epix/window/components.h"
#include "epix/window/resources.h"

using namespace epix;
using namespace epix::window;
using namespace epix::window::components;
using namespace epix::window::resources;

EPIX_API WindowDescription& WindowDescription::set_title(std::string title) {
    this->title = title;
    return *this;
}
EPIX_API WindowDescription& WindowDescription::set_size(int width, int height) {
    this->width  = width;
    this->height = height;
    return *this;
}
EPIX_API WindowDescription& WindowDescription::set_vsync(bool vsync) {
    this->vsync = vsync;
    return *this;
}
EPIX_API WindowDescription& WindowDescription::add_hint(int hint, int value) {
    hints.push_back({hint, value});
    return *this;
}
EPIX_API WindowDescription& WindowDescription::set_hints(
    std::vector<std::pair<int, int>> hints
) {
    this->hints = hints;
    return *this;
}

EPIX_API const Window::dvec2& Window::get_cursor() const {
    return m_cursor_pos;
}
EPIX_API const std::optional<Window::dvec2>& Window::get_cursor_move() const {
    return m_cursor_move;
}
EPIX_API const Window::ivec2& Window::get_pos() const { return m_pos; }
EPIX_API const Window::extent& Window::get_size() const { return m_size; }
EPIX_API GLFWwindow* Window::get_handle() const { return m_handle; }
EPIX_API void Window::context_current() { glfwMakeContextCurrent(m_handle); }
EPIX_API void Window::detach_context() {
    if (glfwGetCurrentContext() == m_handle) glfwMakeContextCurrent(nullptr);
}
EPIX_API void Window::show() { glfwShowWindow(m_handle); }
EPIX_API void Window::hide() { glfwHideWindow(m_handle); }
EPIX_API bool Window::vsync() const { return m_vsync; }
EPIX_API bool Window::should_close() const {
    if (!m_handle) return true;
    return glfwWindowShouldClose(m_handle);
}
EPIX_API void Window::destroy() {
    glfwDestroyWindow(m_handle);
    m_handle = nullptr;
}
EPIX_API void Window::set_cursor(double x, double y) {
    glfwSetCursorPos(m_handle, x, y);
    m_cursor_pos = dvec2{x, y};
}
EPIX_API bool Window::focused() const { return m_focused; }
EPIX_API bool Window::is_fullscreen() const {
    return glfwGetWindowMonitor(m_handle) != nullptr;
}
EPIX_API void Window::set_fullscreen() {
    auto monitor   = glfwGetPrimaryMonitor();
    auto mode      = glfwGetVideoMode(monitor);
    auto* user_ptr = static_cast<std::pair<Entity, WindowThreadPool*>*>(
        glfwGetWindowUserPointer(m_handle)
    );
    auto&& [entity, pool] = *user_ptr;
    auto ftr              = pool->submit_task([&]() {
        glfwSetWindowMonitor(
            m_handle, monitor, 0, 0, mode->width, mode->height,
            mode->refreshRate
        );
    });
    ftr.wait();
    m_size_cache = m_size;
    m_pos_cache  = m_pos;
    m_size       = {mode->width, mode->height};
    m_pos        = {0, 0};
}
EPIX_API void Window::fullscreen_off() {
    auto mode      = glfwGetVideoMode(glfwGetPrimaryMonitor());
    auto* user_ptr = static_cast<std::pair<Entity, WindowThreadPool*>*>(
        glfwGetWindowUserPointer(m_handle)
    );
    auto&& [entity, pool] = *user_ptr;
    m_size                = m_size_cache;
    m_pos                 = m_pos_cache;
    auto ftr              = pool->submit_task([&]() {
        glfwSetWindowMonitor(
            m_handle, nullptr, m_pos.x, m_pos.y, m_size.width, m_size.height, 0
        );
    });
    ftr.wait();
}
EPIX_API void Window::toggle_fullscreen() {
    if (is_fullscreen()) {
        fullscreen_off();
    } else {
        set_fullscreen();
    }
}

EPIX_API std::optional<Window> components::create_window(
    const WindowDescription& desc
) {
    glfwDefaultWindowHints();
    for (auto [hint, value] : desc.hints) {
        glfwWindowHint(hint, value);
    }
    GLFWwindow* window = glfwCreateWindow(
        desc.width, desc.height, desc.title.c_str(), nullptr, nullptr
    );
    if (!window) {
        return std::nullopt;
    }
    int x, y;
    glfwGetWindowPos(window, &x, &y);
    return Window{
        window, desc.title, desc.vsync, {desc.width, desc.height}, {x, y}
    };
}
void update_focused(Window& window) {
    auto* user_ptr = static_cast<std::pair<Entity, WindowThreadPool*>*>(
        glfwGetWindowUserPointer(window.m_handle)
    );
    auto&& [entity, pool] = *user_ptr;
    if (!window.m_get_focus) {
        auto handle        = window.m_handle;
        window.m_get_focus = pool->submit_task([handle]() {
            return glfwGetWindowAttrib(handle, GLFW_FOCUSED) == GLFW_TRUE;
        });
    }
    if (window.m_get_focus.has_value() &&
        window.m_get_focus->wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        window.m_focused = window.m_get_focus->get();
        window.m_get_focus.reset();
    }
}
void update_cursor(Window& window) {
    double x, y;
    glfwGetCursorPos(window.m_handle, &x, &y);
    if (x != window.m_cursor_pos.x || y != window.m_cursor_pos.y) {
        window.m_cursor_move = {
            x - window.m_cursor_pos.x, y - window.m_cursor_pos.y
        };
    } else {
        window.m_cursor_move = std::nullopt;
    }
    window.m_cursor_pos = {x, y};
}
void update_size(Window& window) {
    if (window.is_fullscreen()) return;
    int width, height;
    glfwGetWindowSize(window.m_handle, &width, &height);
    window.m_size = {width, height};
}
void update_pos(Window& window) {
    if (window.is_fullscreen()) return;
    int x, y;
    glfwGetWindowPos(window.m_handle, &x, &y);
    window.m_pos = {x, y};
}
EPIX_API void components::update_state(Window& window) {
    update_cursor(window);
    update_size(window);
    update_pos(window);
    update_focused(window);
}
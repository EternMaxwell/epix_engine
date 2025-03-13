#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
// volk before glfw
#include <GLFW/glfw3.h>
#include <epix/app.h>

namespace epix {
namespace window {
namespace components {
using namespace prelude;

struct PrimaryWindow {};

struct Window;

struct WindowDescription {
    std::string title = "Pixel Engine";
    int width = 480 * 3, height = 270 * 3;
    bool vsync = true;
    std::vector<std::pair<int, int>> hints;

   public:
    EPIX_API WindowDescription& set_title(std::string title);
    EPIX_API WindowDescription& set_size(int width, int height);
    EPIX_API WindowDescription& set_vsync(bool vsync);
    EPIX_API WindowDescription& add_hint(int hint, int value);
    EPIX_API WindowDescription& set_hints(std::vector<std::pair<int, int>> hints
    );
};

struct Window {
    struct extent {
        int width;
        int height;
    };

    struct ivec2 {
        int x;
        int y;
    };

    struct dvec2 {
        double x;
        double y;
    };

    GLFWwindow* m_handle;
    std::string m_title;
    bool m_vsync;
    extent m_size;
    ivec2 m_pos;
    extent m_size_cache;
    ivec2 m_pos_cache;
    dvec2 m_cursor_pos;
    bool m_focused;
    std::optional<std::future<bool>> m_get_focus;
    std::optional<dvec2> m_cursor_move;

   public:
    EPIX_API const dvec2& get_cursor() const;
    EPIX_API const std::optional<dvec2>& get_cursor_move() const;
    EPIX_API const ivec2& get_pos() const;
    EPIX_API const extent& get_size() const;
    EPIX_API GLFWwindow* get_handle() const;
    EPIX_API void context_current();
    EPIX_API void detach_context();
    EPIX_API void show();
    EPIX_API void hide();
    EPIX_API bool vsync() const;
    EPIX_API bool should_close() const;
    EPIX_API void destroy();
    EPIX_API void set_cursor(double x, double y);
    EPIX_API bool focused() const;
    EPIX_API bool is_fullscreen() const;
    EPIX_API void set_fullscreen();
    EPIX_API void fullscreen_off();
    EPIX_API void toggle_fullscreen();
};

EPIX_API std::optional<Window> create_window(const WindowDescription& desc);
EPIX_API void update_state(Window& window);
}  // namespace components
}  // namespace window
}  // namespace epix
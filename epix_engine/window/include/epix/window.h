#pragma once

#include <GLFW/glfw3.h>

#include "epix/app.h"
#include "window/events.h"
#include "window/system.h"
#include "window/window.h"

namespace epix {
namespace window {
using window::Window;
struct Focus {
    std::optional<Entity> focus;

    std::optional<Entity>& operator*() { return focus; }
};
enum class ExitCondition {
    OnAllClosed,
    OnPrimaryClosed,
    None,
};
struct WindowPlugin : public app::Plugin {
    std::optional<Window> primary_window;
    ExitCondition exit_condition = ExitCondition::OnPrimaryClosed;
    bool close_when_requested    = true;
    EPIX_API void build(App& app) override {
        using namespace events;
        app.add_events<WindowResized>()
            .add_events<WindowMoved>()
            .add_events<WindowCreated>()
            .add_events<WindowClosed>()
            .add_events<WindowCloseRequested>()
            .add_events<WindowDestroyed>()
            .add_events<CursorMoved>()
            .add_events<CursorEntered>()
            .add_events<ReceivedCharacter>()
            .add_events<WindowFocused>()
            .add_events<FileDrop>();

        app.insert_resource<Focus>(Focus{std::nullopt});

        if (primary_window) {
            auto window = app.world(epix::app::MainWorld)
                              .spawn(primary_window.value(), PrimaryWindow{});
            if (auto focus =
                    app.world(epix::app::MainWorld).get_resource<Focus>()) {
                ***focus = window;
            }
        }

        if (exit_condition == ExitCondition::OnAllClosed) {
            app.add_systems(PostUpdate, into(exit_on_all_closed));
        } else if (exit_condition == ExitCondition::OnPrimaryClosed) {
            app.add_systems(PostUpdate, into(exit_on_primary_closed));
        }

        if (close_when_requested) {
            app.add_systems(Update, into(close_requested));
        }
    }
};
}  // namespace window
namespace glfw {
struct GLFWExecutor {
    epix::thread_pool thread_pool;

    GLFWExecutor() : thread_pool(1) {}
    epix::thread_pool* operator->() { return &thread_pool; }
};
struct SetCustomCursor {
    Entity window;
    assets::UntypedHandle image;
};
struct PresentModeChange {
    Entity window;
    window::window::PresentMode present_mode;
};
struct KeyInput {
    int key;
    int scancode;
    int action;
    int mods;
};
struct CursorPos {
    double x;
    double y;
};
struct CursorEnter {
    bool entered;
};
struct MouseButton {
    int button;
    int action;
    int mods;
};
struct Scroll {
    double xoffset;
    double yoffset;
};
struct PathDrop {
    std::vector<std::string> paths;
};
/**
 * @brief User data for the glfw window.
 *
 * This will include cached events for later update the event queue in app.
 */
struct UserData {
    std::vector<SetCustomCursor> set_custom_cursor;
    std::vector<PresentModeChange> present_mode_change;
    epix::utils::async::ConQueue<KeyInput> key_input;
    epix::utils::async::ConQueue<CursorPos> cursor_pos;
    epix::utils::async::ConQueue<CursorEnter> cursor_enter;
    epix::utils::async::ConQueue<MouseButton> mouse_button;
    epix::utils::async::ConQueue<Scroll> scroll;
    epix::utils::async::ConQueue<PathDrop> drops;
};

struct GLFWwindows {
    entt::dense_map<Entity, GLFWwindow*> windows;
};

struct CachedWindow : public window::Window {};

void sync_window_to_glfw(
    Entity id,
    window::Window& window_desc,
    window::Window& cached_desc,
    GLFWwindow* window
);
void sync_glfw_to_window(
    Entity id,
    window::Window& window_desc,
    window::Window& cached_desc,
    GLFWwindow* window
);
GLFWwindow* create_window(Entity id, window::Window& window_desc) {
    GLFWwindow* window = glfwCreateWindow(
        window_desc.physical_size().first, window_desc.physical_size().second,
        window_desc.title.c_str(), nullptr, nullptr
    );
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwSetWindowUserPointer(window, new UserData{});
    sync_window_to_glfw(id, window_desc, window_desc, window);
    sync_glfw_to_window(id, window_desc, window_desc, window);
    // add callbacks
    glfwSetKeyCallback(
        window,
        [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->key_input.emplace(key, scancode, action, mods);
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
    );
    glfwSetCursorPosCallback(
        window,
        [](GLFWwindow* window, double x, double y) {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->cursor_pos.emplace(x, y);
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
    );
    glfwSetCursorEnterCallback(window, [](GLFWwindow* window, int entered) {
        auto* user_data =
            static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (user_data) {
            user_data->cursor_enter.emplace(entered == GLFW_TRUE);
        } else {
            throw std::runtime_error("Failed to get user data from window");
        }
    });
    glfwSetMouseButtonCallback(
        window,
        [](GLFWwindow* window, int button, int action, int mods) {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->mouse_button.emplace(button, action, mods);
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
    );
    glfwSetScrollCallback(
        window,
        [](GLFWwindow* window, double xoffset, double yoffset) {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->scroll.emplace(xoffset, yoffset);
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
    );
    return window;
}
void sync_window_to_glfw(
    Entity id,
    window::Window& window_desc,
    window::Window& cached_desc,
    GLFWwindow* window
) {
    if (&window_desc != &cached_desc) {
        // update the glfw window
        if (cached_desc.cursor != window_desc.cursor) {
            if (cached_desc.cursor.icon != window_desc.cursor.icon) {
                std::visit(
                    epix::util::visitor{
                        [window](const window::window::StandardCursor& cursor) {
                            auto glfw_cursor_enum_v = [](const window::window::
                                                             StandardCursor&
                                                                 cursor) {
                                switch (cursor) {
                                    case window::window::StandardCursor::Arrow:
                                        return GLFW_ARROW_CURSOR;
                                    case window::window::StandardCursor::IBeam:
                                        return GLFW_IBEAM_CURSOR;
                                    case window::window::StandardCursor::
                                        Crosshair:
                                        return GLFW_CROSSHAIR_CURSOR;
                                    case window::window::StandardCursor::Hand:
                                        return GLFW_POINTING_HAND_CURSOR;
                                    case window::window::StandardCursor::
                                        ResizeAll:
                                        return GLFW_RESIZE_ALL_CURSOR;
                                    case window::window::StandardCursor::
                                        ResizeNS:
                                        return GLFW_RESIZE_NS_CURSOR;
                                    case window::window::StandardCursor::
                                        ResizeEW:
                                        return GLFW_RESIZE_EW_CURSOR;
                                    case window::window::StandardCursor::
                                        ResizeNWSE:
                                        return GLFW_RESIZE_NWSE_CURSOR;
                                    case window::window::StandardCursor::
                                        ResizeNESW:
                                        return GLFW_RESIZE_NESW_CURSOR;
                                    case window::window::StandardCursor::
                                        NotAllowed:
                                        return GLFW_NOT_ALLOWED_CURSOR;
                                }
                            }(cursor);
                            glfwSetCursor(
                                window,
                                glfwCreateStandardCursor(glfw_cursor_enum_v)
                            );
                        },
                        [window, id](const assets::UntypedHandle& cursor) {
                            auto* user_data = static_cast<UserData*>(
                                glfwGetWindowUserPointer(window)
                            );
                            if (user_data) {
                                user_data->set_custom_cursor.push_back(
                                    SetCustomCursor{id, cursor}
                                );
                            } else {
                                throw std::runtime_error(
                                    "Failed to get user data from window"
                                );
                            }
                        }
                    },
                    window_desc.cursor.icon
                );
            }
            if (cached_desc.cursor.mode != window_desc.cursor.mode) {
                auto glfw_cursor_mode_enum_v =
                    [](const window::window::CursorMode& mode) {
                        switch (mode) {
                            case window::window::CursorMode::Normal:
                                return GLFW_CURSOR_NORMAL;
                            case window::window::CursorMode::Hidden:
                                return GLFW_CURSOR_HIDDEN;
                            case window::window::CursorMode::Captured:
                                return GLFW_CURSOR_CAPTURED;
                            case window::window::CursorMode::Disabled:
                                return GLFW_CURSOR_DISABLED;
                        }
                    }(window_desc.cursor.mode);
                glfwSetInputMode(window, GLFW_CURSOR, glfw_cursor_mode_enum_v);
            }
        }
        if (cached_desc.present_mode != window_desc.present_mode) {
            // present mode is managed by wgpu-native, so send event out
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->present_mode_change.push_back(
                    PresentModeChange{id, window_desc.present_mode}
                );
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
        if (window_desc.monitor != cached_desc.monitor) {
            int count;
            auto monitors = glfwGetMonitors(&count);
            window_desc.monitor =
                window_desc.monitor < count ? window_desc.monitor : 0;
            cached_desc.mode = window::window::WindowMode::Windowed;
        }
        if (cached_desc.position != window_desc.position &&
            window_desc.mode == window::window::WindowMode::Windowed) {
            // position only make sense when windowed
            std::optional<std::pair<int, int>> position =
                window_desc.position.has_value()
                    ? std::make_optional<std::pair<int, int>>(std::make_pair(
                          window_desc.position->x, window_desc.position->y
                      ))
                    : std::nullopt;
            if (!position) {
                // centered to the monitor
                int count;
                auto monitors = glfwGetMonitors(&count);
                window_desc.monitor =
                    window_desc.monitor < count ? window_desc.monitor : 0;
                GLFWmonitor* monitor    = monitors[window_desc.monitor];
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                position =
                    std::make_optional<std::pair<int, int>>(std::make_pair(
                        mode->width / 2 - window_desc.width() / 2,
                        mode->height / 2 - window_desc.height() / 2
                    ));
            }
            glfwSetWindowPos(window, position->first, position->second);
            window_desc.set_position(position->first, position->second);
        }
        if (cached_desc.physical_size() != window_desc.physical_size() &&
            window_desc.mode != window::window::WindowMode::Windowed) {
            // physical size only make sense when windowed
            glfwSetWindowSize(
                window, window_desc.physical_size().first,
                window_desc.physical_size().second
            );
        }
        if (cached_desc.mode != window_desc.mode) {
            if (window_desc.mode == window::window::WindowMode::Windowed) {
                // from fullscreen set back to windowed, this should use the
                // position and sizes in cached_desc
                auto [width, height] = cached_desc.physical_size();
                int x, y;
                if (cached_desc.position) {
                    x = cached_desc.position->x;
                    y = cached_desc.position->y;
                } else {
                    GLFWmonitor* monitor    = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    x                       = mode->width / 2 - width / 2;
                    y                       = mode->height / 2 - height / 2;
                }
                /**
                 * @brief make sure they are larger than 0 to avoid title bar
                 * from going out of the screen
                 */
                x = std::max(0, x);
                y = std::max(0, y);
                glfwSetWindowMonitor(window, nullptr, x, y, width, height, 0);
            } else if (window_desc.mode ==
                       window::window::WindowMode::BorderlessFullscreen) {
                int count;
                auto monitors = glfwGetMonitors(&count);
                window_desc.monitor =
                    window_desc.monitor < count ? window_desc.monitor : 0;
                GLFWmonitor* monitor    = monitors[window_desc.monitor];
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(
                    window, monitor, 0, 0, mode->width, mode->height,
                    mode->refreshRate
                );
            } else if (window_desc.mode ==
                       window::window::WindowMode::Fullscreen) {
                int count;
                auto monitors = glfwGetMonitors(&count);
                window_desc.monitor =
                    window_desc.monitor < count ? window_desc.monitor : 0;
                GLFWmonitor* monitor    = monitors[window_desc.monitor];
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(
                    window, monitor, 0, 0, mode->width, mode->height,
                    mode->refreshRate
                );
            }
        }
        if (cached_desc.size_limit != window_desc.size_limit) {
            glfwSetWindowSizeLimits(
                window, window_desc.size_limit.min_width,
                window_desc.size_limit.min_height,
                window_desc.size_limit.max_width,
                window_desc.size_limit.max_height
            );
        }
        if (cached_desc.resizable != window_desc.resizable) {
            glfwSetWindowAttrib(
                window, GLFW_RESIZABLE,
                window_desc.resizable ? GLFW_TRUE : GLFW_FALSE
            );
        }
        if (cached_desc.decorations != window_desc.decorations) {
            glfwSetWindowAttrib(
                window, GLFW_DECORATED,
                window_desc.decorations ? GLFW_TRUE : GLFW_FALSE
            );
        }
        if (cached_desc.focused != window_desc.focused) {
            if (window_desc.focused) {
                glfwFocusWindow(window);
            } else {
                glfwSetWindowAttrib(
                    window, GLFW_FOCUSED, GLFW_FALSE
                );  // this is not working
            }
        }
        if (cached_desc.visible != window_desc.visible) {
            if (window_desc.visible) {
                glfwShowWindow(window);
            } else {
                glfwHideWindow(window);
            }
        }
        if (cached_desc.opacity != window_desc.opacity) {
            glfwSetWindowOpacity(window, window_desc.opacity);
        }
        if (cached_desc.transparent != window_desc.transparent) {
            glfwSetWindowAttrib(
                window, GLFW_TRANSPARENT_FRAMEBUFFER,
                window_desc.transparent ? GLFW_TRUE : GLFW_FALSE
            );
        }
        if (cached_desc.window_level != window_desc.window_level) {
            auto glfw_window_level_enum_v =
                [](const window::window::WindowLevel& level) {
                    switch (level) {
                        case window::window::WindowLevel::Normal:
                            return GLFW_FALSE;
                        case window::window::WindowLevel::AlwaysOnTop:
                            return GLFW_TRUE;
                        case window::window::WindowLevel::AlwaysOnBottom:
                            return GLFW_FALSE;
                    }
                }(window_desc.window_level);
            glfwSetWindowAttrib(
                window, GLFW_FLOATING, glfw_window_level_enum_v
            );
        }
        if (cached_desc.title != window_desc.title) {
            glfwSetWindowTitle(window, window_desc.title.c_str());
        }
        if (cached_desc.iconified != window_desc.iconified) {
            if (window_desc.iconified) {
                glfwIconifyWindow(window);
            } else {
                glfwRestoreWindow(window);
            }
        }
        // internal state
        if (window_desc.internal.maximize_request) {
            auto maximize_request =
                window_desc.internal.maximize_request.value();
            window_desc.internal.maximize_request.reset();
            if (maximize_request) {
                glfwMaximizeWindow(window);
            } else {
                glfwRestoreWindow(window);
            }
        }
        if (window_desc.internal.attention_request) {
            auto attention_request =
                window_desc.internal.attention_request.value();
            window_desc.internal.attention_request.reset();
            if (attention_request) {
                glfwRequestWindowAttention(window);
            }
        }
        if (window_desc.internal.cursor_position !=
            cached_desc.internal.cursor_position) {
            auto&& [_, x, y] = window_desc.cursor_position();
            glfwSetCursorPos(window, x, y);
        }
    } else {
        // force the glfw window to sync with the window_desc
        {
            std::visit(
                epix::util::visitor{
                    [window](const window::window::StandardCursor& cursor) {
                        auto glfw_cursor_enum_v = [](const window::window::
                                                         StandardCursor& cursor
                                                  ) {
                            switch (cursor) {
                                case window::window::StandardCursor::Arrow:
                                    return GLFW_ARROW_CURSOR;
                                case window::window::StandardCursor::IBeam:
                                    return GLFW_IBEAM_CURSOR;
                                case window::window::StandardCursor::Crosshair:
                                    return GLFW_CROSSHAIR_CURSOR;
                                case window::window::StandardCursor::Hand:
                                    return GLFW_POINTING_HAND_CURSOR;
                                case window::window::StandardCursor::ResizeAll:
                                    return GLFW_RESIZE_ALL_CURSOR;
                                case window::window::StandardCursor::ResizeNS:
                                    return GLFW_RESIZE_NS_CURSOR;
                                case window::window::StandardCursor::ResizeEW:
                                    return GLFW_RESIZE_EW_CURSOR;
                                case window::window::StandardCursor::ResizeNWSE:
                                    return GLFW_RESIZE_NWSE_CURSOR;
                                case window::window::StandardCursor::ResizeNESW:
                                    return GLFW_RESIZE_NESW_CURSOR;
                                case window::window::StandardCursor::NotAllowed:
                                    return GLFW_NOT_ALLOWED_CURSOR;
                            }
                        }(cursor);
                        glfwSetCursor(
                            window, glfwCreateStandardCursor(glfw_cursor_enum_v)
                        );
                    },
                    [window, id](const assets::UntypedHandle& cursor) {
                        auto* user_data = static_cast<UserData*>(
                            glfwGetWindowUserPointer(window)
                        );
                        if (user_data) {
                            user_data->set_custom_cursor.push_back(
                                SetCustomCursor{id, cursor}
                            );
                        } else {
                            throw std::runtime_error(
                                "Failed to get user data from window"
                            );
                        }
                    }
                },
                window_desc.cursor.icon
            );
        }
        {
            auto glfw_cursor_mode_enum_v =
                [](const window::window::CursorMode& mode) {
                    switch (mode) {
                        case window::window::CursorMode::Normal:
                            return GLFW_CURSOR_NORMAL;
                        case window::window::CursorMode::Hidden:
                            return GLFW_CURSOR_HIDDEN;
                        case window::window::CursorMode::Captured:
                            return GLFW_CURSOR_CAPTURED;
                        case window::window::CursorMode::Disabled:
                            return GLFW_CURSOR_DISABLED;
                    }
                }(window_desc.cursor.mode);
            glfwSetInputMode(window, GLFW_CURSOR, glfw_cursor_mode_enum_v);
        }
        {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->present_mode_change.push_back(
                    PresentModeChange{id, window_desc.present_mode}
                );
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
        {
            int count;
            auto monitors = glfwGetMonitors(&count);
            window_desc.monitor =
                window_desc.monitor < count ? window_desc.monitor : 0;
            cached_desc.mode = window::window::WindowMode::Windowed;
        }
        {
            if (window_desc.mode == window::window::WindowMode::Windowed) {
                // from fullscreen set back to windowed, this should use the
                // position and sizes in cached_desc
                int x, y;
                if (cached_desc.position) {
                    x = cached_desc.position->x;
                    y = cached_desc.position->y;
                } else {
                    GLFWmonitor* monitor    = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                    x = mode->width / 2 - cached_desc.physical_size().first / 2;
                    y = mode->height / 2 -
                        cached_desc.physical_size().second / 2;
                }
                /**
                 * @brief make sure they are larger than 0 to avoid title bar
                 * from going out of the screen
                 */
                x = std::max(0, x);
                y = std::max(0, y);
                glfwSetWindowMonitor(
                    window, nullptr, x, y, cached_desc.physical_size().first,
                    cached_desc.physical_size().second, 0
                );
            } else if (window_desc.mode ==
                       window::window::WindowMode::BorderlessFullscreen) {
                int count;
                auto monitors = glfwGetMonitors(&count);
                window_desc.monitor =
                    window_desc.monitor < count ? window_desc.monitor : 0;
                GLFWmonitor* monitor    = monitors[window_desc.monitor];
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(
                    window, monitor, 0, 0, mode->width, mode->height,
                    mode->refreshRate
                );
            } else if (window_desc.mode ==
                       window::window::WindowMode::Fullscreen) {
                int count;
                auto monitors = glfwGetMonitors(&count);
                window_desc.monitor =
                    window_desc.monitor < count ? window_desc.monitor : 0;
                GLFWmonitor* monitor    = monitors[window_desc.monitor];
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(
                    window, monitor, 0, 0, mode->width, mode->height,
                    mode->refreshRate
                );
            }
        }
        {
            std::optional<std::pair<int, int>> position =
                window_desc.position.has_value()
                    ? std::make_optional<std::pair<int, int>>(std::make_pair(
                          window_desc.position->x, window_desc.position->y
                      ))
                    : std::nullopt;
            if (!position) {
                // centered to the monitor
                int count;
                auto monitors = glfwGetMonitors(&count);
                window_desc.monitor =
                    window_desc.monitor < count ? window_desc.monitor : 0;
                GLFWmonitor* monitor    = monitors[window_desc.monitor];
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                position =
                    std::make_optional<std::pair<int, int>>(std::make_pair(
                        mode->width / 2 - window_desc.width() / 2,
                        mode->height / 2 - window_desc.height() / 2
                    ));
            }
            glfwSetWindowPos(window, position->first, position->second);
            window_desc.set_position(position->first, position->second);
        }
        {
            // size limit
            glfwSetWindowSizeLimits(
                window, window_desc.size_limit.min_width,
                window_desc.size_limit.min_height,
                window_desc.size_limit.max_width,
                window_desc.size_limit.max_height
            );
        }
        {
            // resizable
            glfwSetWindowAttrib(
                window, GLFW_RESIZABLE,
                window_desc.resizable ? GLFW_TRUE : GLFW_FALSE
            );
        }
        {
            // decorations
            glfwSetWindowAttrib(
                window, GLFW_DECORATED,
                window_desc.decorations ? GLFW_TRUE : GLFW_FALSE
            );
        }
        {
            // focused
            if (window_desc.focused) {
                glfwFocusWindow(window);
            } else {
                glfwSetWindowAttrib(
                    window, GLFW_FOCUSED, GLFW_FALSE
                );  // this is not working
            }
        }
        {
            // visible
            if (window_desc.visible) {
                glfwShowWindow(window);
            } else {
                glfwHideWindow(window);
            }
        }
        {
            // opacity
            glfwSetWindowOpacity(window, window_desc.opacity);
        }
        {
            // transparent
            glfwSetWindowAttrib(
                window, GLFW_TRANSPARENT_FRAMEBUFFER,
                window_desc.transparent ? GLFW_TRUE : GLFW_FALSE
            );
        }
        {
            auto glfw_window_level_enum_v =
                [](const window::window::WindowLevel& level) {
                    switch (level) {
                        case window::window::WindowLevel::Normal:
                            return GLFW_FALSE;
                        case window::window::WindowLevel::AlwaysOnTop:
                            return GLFW_TRUE;
                        case window::window::WindowLevel::AlwaysOnBottom:
                            return GLFW_FALSE;
                    }
                }(window_desc.window_level);
            glfwSetWindowAttrib(
                window, GLFW_FLOATING, glfw_window_level_enum_v
            );
        }
        {
            // iconified
            if (window_desc.iconified) {
                glfwIconifyWindow(window);
            } else {
                glfwRestoreWindow(window);
            }
        }
    }
}
void sync_glfw_to_window(
    Entity id,
    window::Window& window_desc,
    window::Window& cached_desc,
    GLFWwindow* window
) {
    {
        static entt::dense_map<Entity, std::pair<int, int>> fullscreen_size;
        {
            auto res = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
            if (!res && res != window_desc.iconified &&
                window_desc.mode != window::window::WindowMode::Windowed) {
                auto it = fullscreen_size.find(id);
                if (it != fullscreen_size.end()) {
                    auto [width, height] = it->second;
                    glfwSetWindowSize(window, width, height);
                } else {
                    glfwSetWindowSize(
                        window, window_desc.physical_size().first,
                        window_desc.physical_size().second
                    );
                }
            }
            if (res && res != window_desc.iconified &&
                window_desc.mode != window::window::WindowMode::Windowed) {
                fullscreen_size[id] = std::make_pair(
                    window_desc.physical_size().first,
                    window_desc.physical_size().second
                );
            }
            window_desc.iconified = res;
        }
        // update the window_desc using glfw window
        {
            // get the window size
            int width, height;
            glfwGetWindowSize(window, &width, &height);
            int framebuffer_width, framebuffer_height;
            glfwGetFramebufferSize(
                window, &framebuffer_width, &framebuffer_height
            );
            window_desc.window_size.physical_width  = width;
            window_desc.window_size.physical_height = height;
            window_desc.window_size.width           = framebuffer_width;
            window_desc.window_size.height          = framebuffer_height;
        }
        {
            // get the window position
            int x, y;
            glfwGetWindowPos(window, &x, &y);
            window_desc.position =
                std::make_optional<window::window::WindowPosition>(x, y);
        }
        {
            int left, right, top, bottom;
            glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
            window_desc.frame_size.left   = left;
            window_desc.frame_size.right  = right;
            window_desc.frame_size.top    = top;
            window_desc.frame_size.bottom = bottom;
        }
        {
            // update internal
            auto& [in, x, y] = window_desc.internal.cursor_position;
            glfwGetCursorPos(window, &x, &y);
            // check whether cursor is in window
            auto [width, height] = window_desc.physical_size();
            in = x >= 0 && x <= width && y >= 0 && y <= height;
        }
        {
            // get focused
            int focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
            if (focused) {
                window_desc.focused = true;
            } else {
                window_desc.focused = false;
            }
        }
        { window_desc.opacity = glfwGetWindowOpacity(window); }
        // set cached to new window_desc value
        cached_desc.cursor       = window_desc.cursor;
        cached_desc.mode         = window_desc.mode;
        cached_desc.monitor      = window_desc.monitor;
        cached_desc.size_limit   = window_desc.size_limit;
        cached_desc.resizable    = window_desc.resizable;
        cached_desc.decorations  = window_desc.decorations;
        cached_desc.focused      = window_desc.focused;
        cached_desc.visible      = window_desc.visible;
        cached_desc.opacity      = window_desc.opacity;
        cached_desc.transparent  = window_desc.transparent;
        cached_desc.window_level = window_desc.window_level;
        cached_desc.title        = window_desc.title;
        cached_desc.internal     = window_desc.internal;
        cached_desc.frame_size   = window_desc.frame_size;
        cached_desc.iconified    = window_desc.iconified;
        // size and position should be cached when window is fullscreen
        // which means cached value should not be updated
        if (window_desc.mode == window::window::WindowMode::Windowed) {
            cached_desc.window_size = window_desc.window_size;
            cached_desc.position    = window_desc.position;
        }
        if (!cached_desc.position) {
            cached_desc.position = window_desc.position;
        }
    }
}
struct SetClipboardString {
    std::string text;
};
struct Clipboard {
   private:
    std::string text;

   public:
    const std::string& get_text() { return text; }
    static void update(
        ResMut<Clipboard> clipboard, ResMut<GLFWExecutor> executor
    ) {
        auto res = executor->thread_pool.wait_for(std::chrono::seconds(0));
        if (res) {
            executor->thread_pool.detach_task([&]() {
                clipboard->text = std::string(glfwGetClipboardString(nullptr));
            });
            executor->thread_pool.wait();
        }
    }
    static void set_text(
        ResMut<GLFWExecutor> executor, EventReader<SetClipboardString> events
    ) {
        for (auto&& event : events.read()) {
            auto res = executor->thread_pool.wait_for(std::chrono::seconds(0));
            if (res) {
                executor->thread_pool.detach_task([&]() {
                    glfwSetClipboardString(nullptr, event.text.c_str());
                });
                executor->thread_pool.wait();
            }
        }
    }
};
struct GLFWPlugin : public app::Plugin {
    EPIX_API void build(App& app) override {
        app.world(epix::app::MainWorld).init_resource<GLFWExecutor>();
    }
};
}  // namespace glfw
}  // namespace epix
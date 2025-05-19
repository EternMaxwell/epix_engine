#include "epix/window.h"

using namespace epix::glfw;
using namespace epix;
using namespace epix::window;

EPIX_API void epix::glfw::sync_window_to_glfw(
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
                                    default:
                                        return GLFW_ARROW_CURSOR;
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
                            default:
                                return GLFW_CURSOR_NORMAL;
                        }
                    }(window_desc.cursor.mode);
                glfwSetInputMode(window, GLFW_CURSOR, glfw_cursor_mode_enum_v);
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
                        mode->width / 2 - window_desc.physical_size().first / 2,
                        mode->height / 2 -
                            window_desc.physical_size().second / 2
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
        if (cached_desc.iconified != window_desc.iconified) {
            if (window_desc.iconified) {
                glfwIconifyWindow(window);
            } else {
                glfwRestoreWindow(window);
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
                        default:
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
        if (cached_desc.focused != window_desc.focused) {
            if (window_desc.focused) {
                glfwFocusWindow(window);
            } else {
                glfwSetWindowAttrib(
                    window, GLFW_FOCUSED, GLFW_FALSE
                );  // this is not working
            }
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
                                default:
                                    return GLFW_ARROW_CURSOR;
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
                        default:
                            return GLFW_CURSOR_NORMAL;
                    }
                }(window_desc.cursor.mode);
            glfwSetInputMode(window, GLFW_CURSOR, glfw_cursor_mode_enum_v);
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
                        mode->width / 2 - window_desc.physical_size().first / 2,
                        mode->height / 2 -
                            window_desc.physical_size().second / 2
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
            // iconified
            if (window_desc.iconified) {
                glfwIconifyWindow(window);
            } else {
                glfwRestoreWindow(window);
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
            auto glfw_window_level_enum_v =
                [](const window::window::WindowLevel& level) {
                    switch (level) {
                        case window::window::WindowLevel::Normal:
                            return GLFW_FALSE;
                        case window::window::WindowLevel::AlwaysOnTop:
                            return GLFW_TRUE;
                        case window::window::WindowLevel::AlwaysOnBottom:
                            return GLFW_FALSE;
                        default:
                            return GLFW_FALSE;
                    }
                }(window_desc.window_level);
            glfwSetWindowAttrib(
                window, GLFW_FLOATING, glfw_window_level_enum_v
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
    }
}

EPIX_API void epix::glfw::sync_glfw_to_window(
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
            bool focused =
                glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
            if (focused != window_desc.focused) {
                window_desc.focused = focused;
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

EPIX_API GLFWwindow* epix::glfw::create_window(
    Entity id, window::Window& window_desc
) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    // glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
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
    glfwSetWindowSizeCallback(
        window,
        [](GLFWwindow* window, int width, int height) {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                user_data->resized.emplace(width, height);
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
    );
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
    glfwSetDropCallback(
        window,
        [](GLFWwindow* window, int count, const char** paths) {
            auto* user_data =
                static_cast<UserData*>(glfwGetWindowUserPointer(window));
            if (user_data) {
                std::vector<std::string> path_vec;
                for (int i = 0; i < count; ++i) {
                    path_vec.emplace_back(paths[i]);
                }
                user_data->drops.emplace(std::move(path_vec));
            } else {
                throw std::runtime_error("Failed to get user data from window");
            }
        }
    );
    glfwSetCharCallback(window, [](GLFWwindow* window, unsigned int codepoint) {
        auto* user_data =
            static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (user_data) {
            user_data->received_character.emplace(codepoint);
        } else {
            throw std::runtime_error("Failed to get user data from window");
        }
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* window, int focused) {
        auto* user_data =
            static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (user_data) {
            user_data->focused.emplace(focused == GLFW_TRUE);
        } else {
            throw std::runtime_error("Failed to get user data from window");
        }
    });
    glfwSetWindowPosCallback(window, [](GLFWwindow* window, int x, int y) {
        auto* user_data =
            static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (user_data) {
            user_data->moved.emplace(x, y);
        } else {
            throw std::runtime_error("Failed to get user data from window");
        }
    });
    return window;
}

EPIX_API const std::string& Clipboard::get_text() const { return text; }
EPIX_API void Clipboard::update(ResMut<Clipboard> clipboard) {
    const char* str = glfwGetClipboardString(nullptr);
    if (str == nullptr) {
        clipboard->text.clear();
        return;
    }
    clipboard->text = std::string(glfwGetClipboardString(nullptr));
}
EPIX_API void Clipboard::set_text(EventReader<SetClipboardString> events) {
    for (auto&& event : events.read()) {
        glfwSetClipboardString(nullptr, event.text.c_str());
    }
}

EPIX_API void GLFWPlugin::build(App& app) {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    app.insert_resource(Clipboard{})
        .init_resource<GLFWwindows>()
        .init_resource<CachedWindows>()
        .add_events<SetCustomCursor>()
        .add_events<SetClipboardString>()
        .add_plugins(epix::LoopPlugin{})
        .add_systems(
            First,
            into(Clipboard::set_text, Clipboard::update)
                .chain()
                .set_executor(epix::app::ExecutorType::SingleThread)
                .set_names({"set clipboard string", "update clipboard string"})
        )
        .add_systems(
            First, into(
                       window_changed, poll_events, send_cached_events,
                       sync_windows, create_windows
                   )
                       .chain()
                       .set_executor(epix::app::ExecutorType::SingleThread)
                       .set_names(
                           {"window changed", "glfw poll events",
                            "send cached window events", "sync windows",
                            "create windows"}
                       )
        )
        .add_systems(
            Last, into(destroy_windows)
                      .set_executor(epix::app::ExecutorType::SingleThread)
                      .set_name("destroy windows")
        )
        .add_systems(
            Exit, into(destroy_windows)
                      .set_executor(epix::app::ExecutorType::SingleThread)
                      .set_name("destroy windows")
        )
        .add_systems(
            PreUpdate,
            into([](ResMut<window::Focus> focus, Local<window::Focus> last,
                    ResMut<GLFWwindows> glfw_windows) {
                if (focus->focus != last->focus) {
                    if (auto it = glfw_windows->windows.find(*focus->focus);
                        it != glfw_windows->windows.end()) {
                        auto window = it->second;
                        glfwFocusWindow(window);
                    }
                }
                last->focus = focus->focus;
            })
                .set_executor(epix::app::ExecutorType::SingleThread)
                .set_name("glfw focus")
        )
        .add_systems(
            PostExit, into([]() { glfwTerminate(); })
                          .set_executor(epix::app::ExecutorType::SingleThread)
                          .set_name("terminate glfw")
        );
    app.submit_system(ExecutorType::SingleThread, create_windows).get();
}
EPIX_API void GLFWPlugin::create_windows(
    Commands commands,
    Query<Get<Entity, window::window::Window>> windows,
    ResMut<GLFWwindows> glfw_windows,
    ResMut<CachedWindows> cached_windows,
    EventWriter<window::events::WindowCreated>& window_created
) {
    for (auto&& [id, window_desc] :
         windows.iter() | std::views::filter([&](auto&& tuple) {
             auto&& [id, _] = tuple;
             return !glfw_windows->windows.contains(id);
         })) {
        auto window = create_window(id, window_desc);
        window_created.write(window::events::WindowCreated{id});
        glfw_windows->windows.emplace(id, window);
        cached_windows->caches.emplace(id, window_desc);
        commands.entity(id).emplace(window);
    }
}
EPIX_API void GLFWPlugin::window_changed(
    Query<Get<Entity, window::window::Window>> windows,
    ResMut<GLFWwindows> glfw_windows,
    ResMut<CachedWindows> cached_windows
) {
    for (auto&& [id, window_desc] :
         windows.iter() | std::views::filter([&](auto&& tuple) {
             auto&& [id, _] = tuple;
             return glfw_windows->windows.contains(id);
         })) {
        auto window          = glfw_windows->windows[id];
        auto&& cached_window = cached_windows->caches[id];
        sync_window_to_glfw(id, window_desc, cached_window, window);
    }
}
EPIX_API void GLFWPlugin::poll_events() { glfwPollEvents(); }
EPIX_API void GLFWPlugin::sync_windows(
    Query<Get<Entity, window::window::Window>> windows,
    ResMut<GLFWwindows> glfw_windows,
    ResMut<CachedWindows> cached_windows
) {
    for (auto&& [id, window_desc] :
         windows.iter() | std::views::filter([&](auto&& tuple) {
             auto&& [id, _] = tuple;
             return glfw_windows->windows.contains(id);
         })) {
        auto window          = glfw_windows->windows[id];
        auto&& cached_window = cached_windows->caches[id];
        sync_glfw_to_window(id, window_desc, cached_window, window);
    }
}
EPIX_API void GLFWPlugin::send_cached_events(
    ResMut<GLFWwindows> glfw_windows,
    ResMut<CachedWindows> cached_windows,
    EventWriter<SetCustomCursor>& set_custom_cursor,
    EventWriter<window::events::WindowResized>& window_resized,
    EventWriter<window::events::WindowCloseRequested>& window_close_requested,
    EventWriter<window::events::CursorMoved>& cursor_moved,
    EventWriter<window::events::CursorEntered>& cursor_entered,
    EventWriter<window::events::FileDrop>& file_drop,
    EventWriter<window::events::ReceivedCharacter>& received_character,
    EventWriter<window::events::WindowFocused>& window_focused,
    EventWriter<window::events::WindowMoved>& window_moved,
    std::optional<EventWriter<input::events::KeyInput>>& key_input_event,
    std::optional<EventWriter<input::events::MouseButtonInput>>&
        mouse_button_input,
    std::optional<EventWriter<input::events::MouseMove>>& mouse_move_input,
    std::optional<EventWriter<input::events::MouseScroll>>& scroll_input
) {
    for (auto&& [id, window] : glfw_windows->windows) {
        auto* user_data =
            static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (!user_data) {
            throw std::runtime_error("Failed to get user data from window");
        }
        for (auto&& custom_cursor : user_data->set_custom_cursor) {
            // send out
            set_custom_cursor.write(custom_cursor);
        }
        user_data->set_custom_cursor.clear();
        while (auto new_size = user_data->resized.try_pop()) {
            // send out
            window_resized.write(window::events::WindowResized{
                id, new_size->width, new_size->height
            });
        }
        {
            if (glfwWindowShouldClose(window)) {
                window_close_requested.write(
                    window::events::WindowCloseRequested{id}
                );
            }
        }
        while (auto key_input = user_data->key_input.try_pop()) {
            // send out
            auto&& [key, scancode, action, mods] = *key_input;
            auto pressed = action == GLFW_PRESS || action == GLFW_REPEAT;
            auto repeat  = action == GLFW_REPEAT;
            if (key_input_event)
                key_input_event->write(input::events::KeyInput{
                    map_glfw_key_to_input(key), scancode, pressed, repeat, id
                });
        }
        while (auto cursor_pos = user_data->cursor_pos.try_pop()) {
            // send out
            auto [new_x, new_y] = *cursor_pos;
            auto [in, old_x, old_y] =
                cached_windows->caches[id].cursor_position();
            cursor_moved.write(window::events::CursorMoved{
                id, {new_x, new_y}, {new_x - old_x, new_y - old_y}
            });
            if (mouse_move_input)
                mouse_move_input->write(
                    input::events::MouseMove{{new_x - old_x, new_y - old_y}}
                );
        }
        while (auto cursor_enter = user_data->cursor_enter.try_pop()) {
            // send out
            auto&& [enter] = *cursor_enter;
            cursor_entered.write(window::events::CursorEntered{id, enter});
        }
        while (auto mouse_button = user_data->mouse_button.try_pop()) {
            // send out
            auto&& [button, action, mods] = *mouse_button;
            auto pressed                  = action == GLFW_PRESS;
            if (mouse_button_input)
                mouse_button_input->write(input::events::MouseButtonInput{
                    map_glfw_mouse_button_to_input(button), pressed, id
                });
        }
        while (auto scroll = user_data->scroll.try_pop()) {
            // send out
            auto&& [xoffset, yoffset] = *scroll;
            if (scroll_input)
                scroll_input->write(
                    input::events::MouseScroll{xoffset, yoffset, id}
                );
        }
        while (auto paths_drop = user_data->drops.try_pop()) {
            // send out
            auto&& paths = paths_drop->paths;
            file_drop.write(window::events::FileDrop{id, std::move(paths)});
        }
        while (auto character = user_data->received_character.try_pop()) {
            // send out
            auto&& [codepoint] = *character;
            received_character.write(
                window::events::ReceivedCharacter{id, codepoint}
            );
        }
        while (auto focused = user_data->focused.try_pop()) {
            // send out
            window_focused.write(window::events::WindowFocused{id, *focused});
        }
        while (auto moved = user_data->moved.try_pop()) {
            // send out
            auto&& [x, y] = *moved;
            window_moved.write(window::events::WindowMoved{id, {x, y}});
        }
    }
}
EPIX_API void GLFWPlugin::destroy_windows(
    Query<Get<Entity, window::window::Window>> windows,
    ResMut<GLFWwindows> glfw_windows,
    ResMut<CachedWindows> cached_windows,
    EventWriter<window::events::WindowClosed>& window_closed,
    EventWriter<window::events::WindowDestroyed>& window_destroyed
) {
    entt::dense_set<Entity> still_alive;
    for (auto&& [id, window_desc] : windows.iter()) {
        still_alive.insert(id);
    }
    std::vector<Entity> to_erase;
    for (auto&& [id, window] : std::views::all(glfw_windows->windows) |
                                   std::views::filter([&](auto&& tuple) {
                                       auto&& [id, _] = tuple;
                                       return !still_alive.contains(id);
                                   })) {
        window_closed.write(window::events::WindowClosed{id});
        auto user_data =
            static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (user_data) {
            delete user_data;
        }
        glfwDestroyWindow(window);
        to_erase.emplace_back(id);
        cached_windows->caches.erase(id);
        window_destroyed.write(window::events::WindowDestroyed{id});
    }
    for (auto&& id : to_erase) {
        glfw_windows->windows.erase(id);
    }
}
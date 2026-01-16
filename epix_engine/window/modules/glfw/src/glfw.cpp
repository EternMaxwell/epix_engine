module;

#include <GLFW/glfw3.h>

module epix.glfw.core;

using namespace glfw;
using namespace window;

const std::string& Clipboard::get_text() const { return text; }
void Clipboard::update(ResMut<Clipboard> clipboard) {
    const char* str = glfwGetClipboardString(nullptr);
    if (str == nullptr) {
        clipboard->text.clear();
        return;
    }
    clipboard->text = std::string(glfwGetClipboardString(nullptr));
}
void Clipboard::set_text(EventReader<SetClipboardString> events) {
    for (auto&& event : events.read()) {
        glfwSetClipboardString(nullptr, event.text.c_str());
    }
}
GLFWwindow* GLFWPlugin::create_window(Entity id, Window& desc) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    auto [width, height] = desc.size;
    auto window          = glfwCreateWindow(width, height, desc.title.c_str(), nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
    glfwSetWindowPos(window, desc.final_pos.first, desc.final_pos.second);
    // window mode
    int monitor_count = 0;
    auto monitors     = glfwGetMonitors(&monitor_count);
    if (desc.monitor >= monitor_count || desc.monitor < 0) {
        desc.monitor = 0;  // reset to default monitor
    }
    auto monitor    = monitors[desc.monitor];
    auto video_mode = glfwGetVideoMode(monitor);
    if (desc.window_mode == WindowMode::Windowed) {
        // switch to windowed mode
        glfwSetWindowMonitor(window, nullptr, desc.final_pos.first, desc.final_pos.second, desc.size.first,
                             desc.size.second, 0);
    } else if (desc.window_mode == WindowMode::Fullscreen) {
        // switch to fullscreen mode
        glfwSetWindowMonitor(window, monitor, 0, 0, video_mode->width, video_mode->height, video_mode->refreshRate);
    } else if (desc.window_mode == WindowMode::BorderlessFullscreen) {
        // switch to borderless mode
        glfwSetWindowMonitor(window, monitor, 0, 0, video_mode->width, video_mode->height, video_mode->refreshRate);
    }
    glfwSetWindowOpacity(window, desc.opacity);
    glfwSetWindowSizeLimits(window, desc.size_limits.min_width, desc.size_limits.min_height, desc.size_limits.max_width,
                            desc.size_limits.max_height);
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_DECORATED, desc.decorations ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_FOCUSED, desc.focused ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_ICONIFIED, desc.iconified ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_MAXIMIZED, desc.maximized ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowAttrib(window, GLFW_FLOATING, desc.window_level == WindowLevel::AlwaysOnTop ? GLFW_TRUE : GLFW_FALSE);

    glfwSetWindowUserPointer(window, new UserData{});
    // add callbacks
    glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width, int height) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->resized.emplace(width, height);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->key_input.emplace(key, scancode, action, mods);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double x, double y) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->cursor_pos.emplace(x, y);
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow* window, int entered) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->cursor_enter.emplace(entered == GLFW_TRUE);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->mouse_button.emplace(button, action, mods);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* window, double xoffset, double yoffset) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->scroll.emplace(xoffset, yoffset);
    });
    glfwSetDropCallback(window, [](GLFWwindow* window, int count, const char** paths) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        std::vector<std::string> path_vec;
        for (int i = 0; i < count; ++i) {
            path_vec.emplace_back(paths[i]);
        }
        user_data->drops.emplace(std::move(path_vec));
    });
    glfwSetCharCallback(window, [](GLFWwindow* window, unsigned int codepoint) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->received_character.emplace(codepoint);
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* window, int focused) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->focused.emplace(focused == GLFW_TRUE);
    });
    glfwSetWindowPosCallback(window, [](GLFWwindow* window, int x, int y) {
        auto* user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        user_data->moved.emplace(x, y);
    });
    return window;
}

void GLFWPlugin::update_size(Query<Item<Entity, Mut<Window>>> windows, ResMut<GLFWwindows> glfw_windows) {
    for (auto&& [id, mdesc] : windows.iter()) {
        auto& desc                                             = mdesc.get_mut();
        std::optional<std::tuple<GLFWwindow*, Window&>> stored = std::nullopt;
        if (auto it = glfw_windows->find(id); it != glfw_windows->end()) {
            stored.emplace(it->second.first, it->second.second);
        } else {
            continue;  // window not created yet
        }
        auto&& [window, cached] = *stored;
        if (cached.size != desc.size) {
            // size changed, update the window size
            glfwSetWindowSize(window, desc.size.first, desc.size.second);
        }
        // read size from window, cause set might fail if window is fullscreen
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        desc.size   = {width, height};
        cached.size = desc.size;
    }
}
void GLFWPlugin::update_pos(Commands commands,
                            Query<Item<Entity, Mut<Window>, Opt<const Parent&>>> windows,
                            ResMut<GLFWwindows> glfw_windows) {
    /// For all windows in the query, if it has been cached(created), then if
    /// the pos has been changed manually, e.g. different with cached, then
    /// the new pos should be calculated with new final pos and new pos_type if
    /// it is changed. If pos is changed, then recalculate the final pos
    /// based on the pos_type. Otherwise if pos type changed, final pos should
    /// remain the same and recalculate the pos based on the pos_type.
    /// If it was not cached, then we use the relative pos to calculate the
    /// final pos.
    std::unordered_map<Entity, std::pair<int, int>> final_positions;
    int monitor_count = 0;
    auto monitors     = glfwGetMonitors(&monitor_count);
    std::function<std::pair<int, int>(Entity)> calculate;
    calculate = [&](Entity id) -> std::pair<int, int> {
        if (final_positions.contains(id)) {
            return final_positions.at(id);
        }
        auto [_, mdesc, rparent]                               = windows.get(id).value();
        auto* parent                                           = rparent.has_value() ? &rparent.value().get() : nullptr;
        auto& desc                                             = mdesc.get_mut();
        std::optional<std::tuple<GLFWwindow*, Window&>> stored = std::nullopt;
        if (auto it = glfw_windows->find(id); it != glfw_windows->end()) {
            stored.emplace(it->second.first, it->second.second);
        }
        if (desc.monitor >= monitor_count || desc.monitor < 0) {
            desc.monitor = 0;  // default to first monitor
        }
        std::optional<std::pair<int, int>> parent_pos;
        if (parent) {
            if (windows.contains(parent->entity()))
                parent_pos = calculate(parent->entity());
            else
                commands.entity(id).remove<Parent>();
        }
        auto* monitor    = monitors[desc.monitor];
        auto* video_mode = glfwGetVideoMode(monitor);
        int monitor_x, monitor_y;
        glfwGetMonitorPos(monitor, &monitor_x, &monitor_y);

        if (stored) {
            auto&& [glfw_window, cached] = *stored;
            if (cached.final_pos != desc.final_pos) {
                // calculate the relevant pos based on pos_type
                if (desc.pos_type == PosType::TopLeft) {
                    desc.pos = {desc.final_pos.first - monitor_x, desc.final_pos.second - monitor_y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        desc.pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    desc.pos = {desc.final_pos.first - monitor_x - (video_mode->width - desc.size.first) / 2,
                                desc.final_pos.second - monitor_y - (video_mode->height - desc.size.second) / 2};
                }
            } else if (cached.pos != desc.pos) {
                // recalculate final pos based on pos_type
                if (desc.pos_type == PosType::TopLeft) {
                    desc.final_pos = {desc.pos.first + monitor_x, desc.pos.second + monitor_y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        desc.final_pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
                    } else {
                        desc.pos_type = PosType::Centered;
                        desc.pos      = {0, 0};
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    desc.final_pos = {desc.pos.first + monitor_x + (video_mode->width - desc.size.first) / 2,
                                      desc.pos.second + monitor_y + (video_mode->height - desc.size.second) / 2};
                }
            } else if (cached.pos_type != desc.pos_type) {
                // recalculate pos based on pos_type
                if (desc.pos_type == PosType::TopLeft) {
                    desc.pos = {desc.final_pos.first - monitor_x, desc.final_pos.second - monitor_y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        desc.pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    desc.pos = {desc.final_pos.first - (video_mode->width - desc.size.first) / 2,
                                desc.final_pos.second - (video_mode->height - desc.size.second) / 2};
                }
            }
            // if this window has a parent, we need to recalculate the final pos
            // anyway
            if (desc.pos_type == PosType::Relative && parent_pos) {
                desc.final_pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
            }
            if (desc.final_pos != cached.final_pos || desc.pos != cached.pos || desc.pos_type != cached.pos_type) {
                // set the new position to the glfw window
                glfwSetWindowPos(glfw_window, desc.final_pos.first, desc.final_pos.second);
                cached.final_pos = desc.final_pos;
                cached.pos       = desc.pos;
                cached.pos_type  = desc.pos_type;
            }
            {
                // read from glfw window, and guarantee that the ultimate
                // position is correct.
                glfwGetWindowPos(glfw_window, &desc.final_pos.first, &desc.final_pos.second);
                cached.final_pos = desc.final_pos;
                // calculate the pos based on final_pos
                const auto& final_pos = desc.final_pos;
                std::pair<int, int> pos;
                if (desc.pos_type == PosType::TopLeft) {
                    pos = {final_pos.first - monitor_x, final_pos.second - monitor_y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        pos = {final_pos.first - parent_pos->first, final_pos.second - parent_pos->second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    pos = {final_pos.first - (video_mode->width - desc.size.first) / 2,
                           final_pos.second - (video_mode->height - desc.size.second) / 2};
                }
                desc.pos            = pos;
                cached.final_pos    = desc.final_pos;
                cached.pos          = desc.pos;
                cached.pos_type     = desc.pos_type;
                final_positions[id] = desc.final_pos;
                return desc.final_pos;
            }
        } else {
            // not cached, this means this window has not been created yet.
            if (desc.pos_type == PosType::TopLeft) {
                desc.final_pos = {desc.pos.first + monitor_x, desc.pos.second + monitor_y};
            }
            if (desc.pos_type == PosType::Relative) {
                if (parent_pos) {
                    desc.final_pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
                } else {
                    desc.pos_type = PosType::Centered;
                    desc.pos      = {0, 0};
                }
            }
            if (desc.pos_type == PosType::Centered) {
                desc.final_pos = {desc.pos.first + monitor_x + (video_mode->width - desc.size.first) / 2,
                                  desc.pos.second + monitor_y + (video_mode->height - desc.size.second) / 2};
            }

            final_positions[id] = desc.final_pos;
            return desc.final_pos;
        }
    };
    for (auto&& [id, desc, parent] : windows.iter()) {
        calculate(id);
    }
}

void GLFWPlugin::create_windows(Commands commands,
                                Query<Item<Entity, Mut<Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                                ResMut<GLFWwindows> glfw_windows,
                                EventWriter<WindowCreated> window_created) {
    for (auto&& [id, desc, parent, children] : windows.iter()) {
        if (glfw_windows->contains(id)) {
            continue;
        }
        auto* created      = create_window(id, desc);
        Window cached      = desc;
        cached.cursor_icon = StandardCursor::Arrow;  // this was not set when create.
        cached.cursor_mode = CursorMode::Normal;     // this was not set when create.
        cached.icon        = std::nullopt;           // this was not set when create.
        glfw_windows->emplace(id, std::make_pair(created, cached));
        window_created.write(WindowCreated{id});
    }
}

void GLFWPlugin::update_window_states(Query<Item<Entity, Mut<Window>>> windows,
                                      Res<assets::Assets<image::Image>> images,
                                      ResMut<GLFWwindows> glfw_windows) {
    for (auto&& [id, mdesc] : windows.iter()) {
        auto& desc                                             = mdesc.get_mut();
        std::optional<std::tuple<GLFWwindow*, Window&>> stored = std::nullopt;
        if (auto it = glfw_windows->find(id); it != glfw_windows->end()) {
            stored.emplace(it->second.first, it->second.second);
        } else {
            continue;  // window not created yet
        }
        auto&& [window, cached] = *stored;

        // resizable
        if (cached.resizable != desc.resizable) {
            glfwSetWindowAttrib(window, GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
            cached.resizable = desc.resizable;
        }
        // decorations
        if (cached.decorations != desc.decorations) {
            glfwSetWindowAttrib(window, GLFW_DECORATED, desc.decorations ? GLFW_TRUE : GLFW_FALSE);
            cached.decorations = desc.decorations;
        }
        // visible
        if (cached.visible != desc.visible) {
            glfwSetWindowAttrib(window, GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
            cached.visible = desc.visible;
        }
        // opacity
        if (cached.opacity != desc.opacity) {
            glfwSetWindowOpacity(window, desc.opacity);
            cached.opacity = desc.opacity;
        }
        // size limits
        if (cached.size_limits != desc.size_limits) {
            glfwSetWindowSizeLimits(window, desc.size_limits.min_width, desc.size_limits.min_height,
                                    desc.size_limits.max_width, desc.size_limits.max_height);
            cached.size_limits = desc.size_limits;
        }
        // title
        if (cached.title != desc.title) {
            glfwSetWindowTitle(window, desc.title.c_str());
            cached.title = desc.title;
        }

        // request attention
        if (desc.attention_request) glfwRequestWindowAttention(window);
        desc.attention_request   = false;
        cached.attention_request = false;

        // cursor mode
        if (cached.cursor_mode != desc.cursor_mode) {
            glfwSetInputMode(window, GLFW_CURSOR, [mode = desc.cursor_mode] {
                switch (mode) {
                    case CursorMode::Normal:
                        return GLFW_CURSOR_NORMAL;
                    case CursorMode::Hidden:
                        return GLFW_CURSOR_HIDDEN;
                    case CursorMode::Disabled:
                        return GLFW_CURSOR_DISABLED;
                    default:
                        return GLFW_CURSOR_NORMAL;
                }
            }());
            cached.cursor_mode = desc.cursor_mode;
        }
        // cursor icon
        if (cached.cursor_icon != desc.cursor_icon) {
            std::visit(assets::visitor{[&](const StandardCursor& icon) {
                                           auto cursor = glfwCreateStandardCursor([icon] {
                                               switch (icon) {
                                                   case StandardCursor::Arrow:
                                                       return GLFW_ARROW_CURSOR;
                                                   case StandardCursor::IBeam:
                                                       return GLFW_IBEAM_CURSOR;
                                                   case StandardCursor::Crosshair:
                                                       return GLFW_CROSSHAIR_CURSOR;
                                                   case StandardCursor::Hand:
                                                       return GLFW_HAND_CURSOR;
                                                   case StandardCursor::ResizeAll:
                                                       return GLFW_RESIZE_ALL_CURSOR;
                                                   case StandardCursor::ResizeNS:
                                                       return GLFW_RESIZE_NS_CURSOR;
                                                   case StandardCursor::ResizeEW:
                                                       return GLFW_RESIZE_EW_CURSOR;
                                                   case StandardCursor::ResizeNWSE:
                                                       return GLFW_RESIZE_NWSE_CURSOR;
                                                   case StandardCursor::ResizeNESW:
                                                       return GLFW_RESIZE_NESW_CURSOR;
                                                   case StandardCursor::NotAllowed:
                                                       return GLFW_NOT_ALLOWED_CURSOR;
                                                   default:
                                                       return GLFW_ARROW_CURSOR;  // default case
                                               }
                                           }());
                                           glfwSetCursor(window, cursor);
                                           cached.cursor_icon = desc.cursor_icon;
                                       },
                                       [&](const CustomCursor& handle) {
                                           images->get(handle.image).transform([&](const image::Image& img) {
                                               auto expected_format = image::Format::RGBA8;
                                               std::optional<image::Image> converted_img;
                                               if (img.format() != expected_format) {
                                                   converted_img = img.convert(expected_format);
                                               }
                                               auto& image = converted_img.has_value() ? *converted_img : img;
                                               auto view   = image.raw_view();
                                               GLFWimage glfw_img;
                                               glfw_img.width  = image.width();
                                               glfw_img.height = image.height();
                                               glfw_img.pixels = const_cast<unsigned char*>(
                                                   reinterpret_cast<const unsigned char*>(view.data()));
                                               auto cursor = glfwCreateCursor(&glfw_img, handle.hot_x, handle.hot_y);
                                               cached.cursor_icon = desc.cursor_icon;
                                               return std::cref(img);
                                           });
                                       }},
                       desc.cursor_icon);
        }
        if (cached.icon != desc.icon) {
            if (desc.icon.has_value()) {
                images->get(desc.icon.value()).transform([&](const image::Image& img) {
                    auto expected_format = image::Format::RGBA8;
                    std::optional<image::Image> converted_img;
                    if (img.format() != expected_format) {
                        converted_img = img.convert(expected_format);
                    }
                    auto& image = converted_img.has_value() ? *converted_img : img;
                    GLFWimage glfw_img;
                    glfw_img.width  = image.width();
                    glfw_img.height = image.height();
                    auto view       = image.raw_view();
                    glfw_img.pixels = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(view.data()));
                    glfwSetWindowIcon(window, 1, &glfw_img);
                    cached.icon = desc.icon;
                    return std::cref(img);
                });
            } else {
                // reset to default icon
                glfwSetWindowIcon(window, 0, nullptr);
                cached.icon = desc.icon;
            }
        }

        // frame size
        glfwGetWindowFrameSize(window, &desc.frame_size.left, &desc.frame_size.top, &desc.frame_size.right,
                               &desc.frame_size.bottom);
        cached.frame_size = desc.frame_size;

        // cursor position
        if (cached.cursor_pos != desc.cursor_pos) {
            glfwSetCursorPos(window, desc.cursor_pos.first, desc.cursor_pos.second);
            cached.cursor_pos = desc.cursor_pos;
        } else {
            glfwGetCursorPos(window, &desc.cursor_pos.first, &desc.cursor_pos.second);
            cached.cursor_pos = desc.cursor_pos;
        }
        // cursor in window
        desc.cursor_in_window = glfwGetWindowAttrib(window, GLFW_HOVERED) == GLFW_TRUE;
        // maximized
        if (cached.maximized != desc.maximized) {
            if (desc.maximized) {
                glfwMaximizeWindow(window);
            } else {
                glfwRestoreWindow(window);
            }
            cached.maximized = desc.maximized;
        }
        // iconified
        if (cached.iconified != desc.iconified) {
            if (desc.iconified) {
                glfwIconifyWindow(window);
            } else {
                glfwRestoreWindow(window);
            }
            cached.iconified = desc.iconified;
        }
        desc.maximized   = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
        desc.iconified   = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
        cached.maximized = desc.maximized;
        cached.iconified = desc.iconified;
        // window level
        if (cached.window_level != desc.window_level) {
            glfwSetWindowAttrib(window, GLFW_FLOATING,
                                desc.window_level == WindowLevel::AlwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
            cached.window_level = desc.window_level;
        } else {
            desc.window_level = glfwGetWindowAttrib(window, GLFW_FLOATING) == GLFW_TRUE ? WindowLevel::AlwaysOnTop
                                                                                        : WindowLevel::Normal;
        }

        // other none glfw handled properties
        cached.composite_alpha_mode = desc.composite_alpha_mode;
        cached.present_mode         = desc.present_mode;
    }
    // focused will be handled here, cause changing one window's focus will
    // change the focus of all windows.
    for (auto&& [id, mdesc] : windows.iter()) {
        auto& desc = mdesc.get();
        if (auto it = glfw_windows->find(id); it != glfw_windows->end()) {
            auto&& [window, cached] = it->second;
            if (cached.focused != desc.focused) {
                glfwSetWindowAttrib(window, GLFW_FOCUSED, desc.focused ? GLFW_TRUE : GLFW_FALSE);
                cached.focused = desc.focused;
            }
        }
    }
    for (auto&& [id, mdesc] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = glfw_windows->find(id); it != glfw_windows->end()) {
            auto&& [window, cached] = it->second;
            // read focused state from glfw window
            desc.focused   = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
            cached.focused = desc.focused;
        }
    }
}

void GLFWPlugin::toggle_window_mode(Query<Item<Entity, Mut<Window>>> windows,
                                    ResMut<GLFWwindows> glfw_windows,
                                    Local<std::unordered_map<Entity, CachedWindowPosSize>> cached_window_sizes) {
    for (auto&& [id, mdesc] : windows.iter()) {
        auto& desc                                             = mdesc.get_mut();
        std::optional<std::tuple<GLFWwindow*, Window&>> stored = std::nullopt;
        if (auto it = glfw_windows->find(id); it != glfw_windows->end()) {
            stored.emplace(it->second.first, it->second.second);
        } else {
            // not created, but when created, it will automatically
            // set the window mode based on the description. so we need to
            // store the cached size now.
            if (desc.window_mode != WindowMode::Windowed) {
                (*cached_window_sizes)[id] =
                    CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first, desc.size.second};
            }
            continue;  // window not created yet
        }
        auto&& [window, cached] = *stored;
        if (desc.window_mode != cached.window_mode) {
            if (desc.window_mode == WindowMode::Windowed) {
                // switch from fullscreen or borderless to windowed mode
                if (cached_window_sizes->contains(id)) {
                    auto& cached_size = cached_window_sizes->at(id);
                    glfwSetWindowMonitor(window, nullptr, cached_size.pos_x, cached_size.pos_y, cached_size.width,
                                         cached_size.height, 0);
                    // remove cached size
                    cached_window_sizes->erase(id);
                } else {
                    // fallback to default size
                    glfwSetWindowMonitor(window, nullptr, 100, 100, 1280, 720, 0);
                }
            } else if (desc.window_mode == WindowMode::Fullscreen) {
                // switch to fullscreen mode
                int monitor_count = 0;
                auto monitors     = glfwGetMonitors(&monitor_count);
                if (desc.monitor >= monitor_count || desc.monitor < 0) {
                    desc.monitor = 0;  // default to first monitor
                }
                auto* monitor    = monitors[desc.monitor];
                auto* video_mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0, video_mode->width, video_mode->height,
                                     video_mode->refreshRate);
                cached_window_sizes->emplace(id, CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second,
                                                                     desc.size.first, desc.size.second});
            } else if (desc.window_mode == WindowMode::BorderlessFullscreen) {
                // switch to borderless mode
                int monitor_count = 0;
                auto monitors     = glfwGetMonitors(&monitor_count);
                if (desc.monitor >= monitor_count || desc.monitor < 0) {
                    desc.monitor = 0;  // default to first monitor
                }
                auto* monitor    = monitors[desc.monitor];
                auto* video_mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0, video_mode->width, video_mode->height,
                                     video_mode->refreshRate);
                cached_window_sizes->emplace(id, CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second,
                                                                     desc.size.first, desc.size.second});
            }
        } else if (cached.monitor != desc.monitor && desc.window_mode != WindowMode::Windowed) {
            // monitor changed, fullscreen or borderless mode
            // should set to the new monitor
            int monitor_count = 0;
            auto monitors     = glfwGetMonitors(&monitor_count);
            if (desc.monitor >= monitor_count || desc.monitor < 0) {
                desc.monitor = 0;  // default to first monitor
            }
            auto* monitor    = monitors[desc.monitor];
            auto* video_mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, monitor, 0, 0, video_mode->width, video_mode->height, video_mode->refreshRate);
            cached_window_sizes->emplace(id, CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second,
                                                                 desc.size.first, desc.size.second});
        }
        cached.window_mode = desc.window_mode;
        // we wont change cached.monitor here cause it still needs to be
        // used to calculate the final position of the window.
    }
}

void GLFWPlugin::poll_events() { glfwPollEvents(); }
void GLFWPlugin::send_cached_events(ResMut<GLFWwindows> glfw_windows,
                                    EventWriter<window::WindowResized> window_resized,
                                    EventWriter<window::WindowCloseRequested> window_close_requested,
                                    EventWriter<window::CursorMoved> cursor_moved,
                                    EventWriter<window::CursorEntered> cursor_entered,
                                    EventWriter<window::FileDrop> file_drop,
                                    EventWriter<window::ReceivedCharacter> received_character,
                                    EventWriter<window::WindowFocused> window_focused,
                                    EventWriter<window::WindowMoved> window_moved,
                                    std::optional<EventWriter<input::KeyInput>> key_input_event,
                                    std::optional<EventWriter<input::MouseButtonInput>> mouse_button_input,
                                    std::optional<EventWriter<input::MouseMove>> mouse_move_input,
                                    std::optional<EventWriter<input::MouseScroll>> scroll_input) {
    for (auto&& [id, pair] : *glfw_windows) {
        auto&& [window, cached] = pair;
        auto* user_data         = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (!user_data) {
            throw std::runtime_error("Failed to get user data from window");
        }
        while (auto new_size = user_data->resized.try_pop()) {
            // send out
            window_resized.write(WindowResized{id, new_size->width, new_size->height});
        }
        {
            if (glfwWindowShouldClose(window)) {
                window_close_requested.write(WindowCloseRequested{id});
            }
        }
        while (auto key_input = user_data->key_input.try_pop()) {
            // send out
            auto&& [key, scancode, action, mods] = *key_input;
            auto pressed                         = action == GLFW_PRESS || action == GLFW_REPEAT;
            auto repeat                          = action == GLFW_REPEAT;
            if (key_input_event)
                key_input_event->write(input::KeyInput{map_glfw_key_to_input(key), scancode, pressed, repeat, id});
        }
        while (auto cursor_pos = user_data->cursor_pos.try_pop()) {
            // send out
            auto [new_x, new_y] = *cursor_pos;
            auto [old_x, old_y] = cached.cursor_pos;
            cursor_moved.write(CursorMoved{id, {new_x, new_y}, {new_x - old_x, new_y - old_y}});
            if (mouse_move_input) mouse_move_input->write(input::MouseMove{{new_x - old_x, new_y - old_y}});
        }
        while (auto cursor_enter = user_data->cursor_enter.try_pop()) {
            // send out
            auto&& [enter] = *cursor_enter;
            cursor_entered.write(CursorEntered{id, enter});
        }
        while (auto mouse_button = user_data->mouse_button.try_pop()) {
            // send out
            auto&& [button, action, mods] = *mouse_button;
            auto pressed                  = action == GLFW_PRESS;
            if (mouse_button_input)
                mouse_button_input->write(input::MouseButtonInput{map_glfw_mouse_button_to_input(button), pressed, id});
        }
        while (auto scroll = user_data->scroll.try_pop()) {
            // send out
            auto&& [xoffset, yoffset] = *scroll;
            if (scroll_input) scroll_input->write(input::MouseScroll{xoffset, yoffset, id});
        }
        while (auto paths_drop = user_data->drops.try_pop()) {
            // send out
            auto&& paths = paths_drop->paths;
            file_drop.write(FileDrop{id, std::move(paths)});
        }
        while (auto character = user_data->received_character.try_pop()) {
            // send out
            auto&& [codepoint] = *character;
            received_character.write(window::ReceivedCharacter{id, codepoint});
        }
        while (auto focused = user_data->focused.try_pop()) {
            // send out
            window_focused.write(WindowFocused{id, *focused});
        }
        while (auto moved = user_data->moved.try_pop()) {
            // send out
            auto&& [x, y] = *moved;
            window_moved.write(WindowMoved{id, {x, y}});
        }
    }
}
void GLFWPlugin::destroy_windows(Query<Item<Entity, const Window&>> windows,
                                 ResMut<GLFWwindows> glfw_windows,
                                 EventWriter<WindowClosed> window_closed,
                                 EventWriter<WindowDestroyed> window_destroyed) {
    std::unordered_set<Entity> still_alive;
    for (auto&& [id, window_desc] : windows.iter()) {
        still_alive.insert(id);
    }
    std::vector<Entity> to_erase;
    for (auto&& [id, glfw_window] : std::views::all(*glfw_windows) | std::views::filter([&](auto&& tuple) {
                                        auto&& [id, _] = tuple;
                                        return !still_alive.contains(id);
                                    })) {
        auto&& [window, cached] = glfw_window;
        window_closed.write(WindowClosed{id});
        auto user_data = static_cast<UserData*>(glfwGetWindowUserPointer(window));
        if (user_data) {
            delete user_data;
        }
        glfwDestroyWindow(window);
        to_erase.emplace_back(id);
        window_destroyed.write(WindowDestroyed{id});
    }
    for (auto&& id : to_erase) {
        glfw_windows->erase(id);
    }
}
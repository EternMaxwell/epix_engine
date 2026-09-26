#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <epix/sdl3/core.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace epix::sdl3;
using namespace epix::window;
using namespace epix::ecs;
using namespace epix::app;

namespace {

/** @brief Cursors this backend created, owned per native window so that a
 * replaced cursor can be destroyed instead of leaking. */
std::unordered_map<SDL_Window*, SDL_Cursor*> owned_cursors;

/** @brief The SDL displays, in the order `Window::monitor` indexes them. */
std::vector<SDL_DisplayID> display_ids() {
    int count           = 0;
    SDL_DisplayID* ids  = SDL_GetDisplays(&count);
    std::vector<SDL_DisplayID> displays;
    if (ids != nullptr) {
        displays.assign(ids, ids + count);
        SDL_free(ids);
    }
    return displays;
}

/** @brief Resolve the entity that owns a native SDL window id.
 *
 * SDL identifies event targets by `SDL_WindowID`; the ECS entity is the
 * authoritative identity, so events are routed through the resource map.
 */
std::optional<Entity> entity_of_window_id(const SDL3windows& windows, SDL_WindowID id) {
    if (id == 0) return std::nullopt;
    SDL_Window* native = SDL_GetWindowFromID(id);
    if (native == nullptr) return std::nullopt;
    for (const auto& [entity, window] : windows) {
        if (window == native) return entity;
    }
    return std::nullopt;
}

/** @brief Decode a UTF-8 string into code points, skipping malformed
 * sequences. */
template <typename Fn>
void for_each_codepoint(std::string_view text, Fn&& fn) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead  = static_cast<unsigned char>(text[i]);
        char32_t code    = 0;
        std::size_t more = 0;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            code = lead & 0x1F;
            more = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            code = lead & 0x0F;
            more = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            code = lead & 0x07;
            more = 3;
        } else {
            ++i;
            continue;
        }
        if (i + more >= text.size()) return;
        bool valid = true;
        for (std::size_t k = 1; k <= more; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (next & 0x3F);
        }
        if (valid) fn(code);
        i += more + 1;
    }
}

/** @brief Set the display mode of an exclusive-fullscreen window.
 *
 * SDL refuses a mode that does not belong to a display, so the display's own
 * fullscreen mode list is preferred and the desktop mode is the fallback.
 */
void apply_exclusive_fullscreen(SDL_Window* window, SDL_DisplayID display) {
    int mode_count             = 0;
    SDL_DisplayMode** modes    = SDL_GetFullscreenDisplayModes(display, &mode_count);
    const SDL_DisplayMode* chosen = nullptr;
    if (modes != nullptr && mode_count > 0) {
        chosen = modes[0];
    }
    if (chosen == nullptr) {
        chosen = SDL_GetDesktopDisplayMode(display);
    }
    SDL_SetWindowFullscreenMode(window, chosen);
    if (modes != nullptr) SDL_free(modes);
    SDL_SetWindowFullscreen(window, true);
}

/** @brief Apply `CursorMode` to a native window. */
void apply_cursor_mode(SDL_Window* window, CursorMode mode) {
    switch (mode) {
        case CursorMode::Normal:
            SDL_SetWindowRelativeMouseMode(window, false);
            SDL_SetWindowMouseRect(window, nullptr);
            SDL_ShowCursor();
            break;
        case CursorMode::Hidden:
            SDL_SetWindowRelativeMouseMode(window, false);
            SDL_SetWindowMouseRect(window, nullptr);
            SDL_HideCursor();
            break;
        case CursorMode::Captured: {
            // SDL confines the pointer to a rectangle rather than exposing a
            // grab flag, so the confinement area is the whole client area.
            SDL_SetWindowRelativeMouseMode(window, false);
            int width  = 0;
            int height = 0;
            SDL_GetWindowSize(window, &width, &height);
            const SDL_Rect rect{0, 0, width, height};
            SDL_SetWindowMouseRect(window, &rect);
            SDL_HideCursor();
            break;
        }
        case CursorMode::Disabled:
            SDL_SetWindowMouseRect(window, nullptr);
            // Only the keyboard-focused window can be in relative mode; a
            // request made while another window holds focus is accepted and
            // dropped.
            if (!SDL_SetWindowRelativeMouseMode(window, true)) {
                spdlog::debug("[sdl3] Relative mouse mode not engaged yet: {}", SDL_GetError());
            }
            break;
    }
}

/** @brief Map a standard cursor shape to its SDL system cursor. */
SDL_SystemCursor to_system_cursor(StandardCursor cursor) {
    switch (cursor) {
        case StandardCursor::Arrow:
            return SDL_SYSTEM_CURSOR_DEFAULT;
        case StandardCursor::IBeam:
            return SDL_SYSTEM_CURSOR_TEXT;
        case StandardCursor::Crosshair:
            return SDL_SYSTEM_CURSOR_CROSSHAIR;
        case StandardCursor::Hand:
            return SDL_SYSTEM_CURSOR_POINTER;
        case StandardCursor::ResizeAll:
            return SDL_SYSTEM_CURSOR_MOVE;
        case StandardCursor::ResizeNS:
            return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case StandardCursor::ResizeEW:
            return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case StandardCursor::ResizeNWSE:
            return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case StandardCursor::ResizeNESW:
            return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case StandardCursor::NotAllowed:
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        default:
            return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

/**
 * @brief The 8-bit RGBA pixels a cursor or window icon needs, following Bevy's
 * `extract_rgba_pixels` (`bevy_winit/src/cursor/custom_cursor.rs:85-103`).
 *
 * The 8-bit RGBA spellings pass their payload through unchanged, `Rgba32Float` is rounded to
 * 8-bit, and every other format has no cursor/icon representation - which is Bevy's `None`, so no
 * cursor is created rather than a payload being reinterpreted.
 */
std::optional<std::vector<std::byte>> extract_rgba_pixels(const epix::image::Image& image) {
    switch (image.texture_format()) {
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
        case wgpu::TextureFormat::eRGBA8Snorm:
        case wgpu::TextureFormat::eRGBA8Uint:
        case wgpu::TextureFormat::eRGBA8Sint: {
            const auto span = image.raw_view();
            return std::vector<std::byte>(span.begin(), span.end());
        }
        case wgpu::TextureFormat::eRGBA32Float: {
            const auto span = image.raw_view();
            std::vector<std::byte> pixels;
            pixels.reserve(span.size() / 4);
            for (std::size_t offset = 0; offset + sizeof(float) <= span.size(); offset += sizeof(float)) {
                float value = 0.0f;
                std::memcpy(&value, span.data() + offset, sizeof(value));
                pixels.push_back(static_cast<std::byte>(
                    static_cast<unsigned char>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f))));
            }
            return pixels;
        }
        default:
            return std::nullopt;
    }
}

/** @brief Install a newly created cursor on a window, replacing the previous
 * one. */
void install_cursor(SDL_Window* window, SDL_Cursor* cursor) {
    if (cursor == nullptr) return;
    if (auto it = owned_cursors.find(window); it != owned_cursors.end()) {
        if (it->second == cursor) return;
        SDL_DestroyCursor(it->second);
        it->second = cursor;
    } else {
        owned_cursors.emplace(window, cursor);
    }
    SDL_SetCursor(cursor);
}

void release_cursor(SDL_Window* window) {
    if (auto it = owned_cursors.find(window); it != owned_cursors.end()) {
        SDL_DestroyCursor(it->second);
        owned_cursors.erase(it);
    }
}

}  // namespace

void SDL3Plugin::update_clipboard(ResMut<window::Clipboard> clipboard, Local<std::string> last_written) {
    // Every access goes through bypass_change_detection: the backend is not a
    // writer of this resource, it only mirrors it, and marking it here would
    // make every reader see a change on every frame.
    window::Clipboard& value = clipboard.bypass_change_detection();
    // A write by another system is the request to set the OS clipboard. The
    // last value this backend pushed is kept so an unchanged one is not written
    // again.
    if (clipboard.is_modified() && value.text != *last_written) {
        SDL_SetClipboardText(value.text.c_str());
        *last_written = value.text;
        return;
    }
    // Otherwise the OS clipboard is the source of truth.
    char* str           = SDL_GetClipboardText();
    std::string os_text = str != nullptr ? std::string(str) : std::string{};
    if (str != nullptr) SDL_free(str);
    if (os_text != value.text) value.text = std::move(os_text);
}

SDL_Window* SDL3Plugin::create_window(Entity id, Window& desc) {
    spdlog::debug("[sdl3] Creating window '{}' ({}x{}) for entity {}.", desc.title, desc.size.first, desc.size.second,
                  id.index);

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (!desc.visible) flags |= SDL_WINDOW_HIDDEN;
    if (desc.window_mode != WindowMode::Windowed || !desc.decorations) flags |= SDL_WINDOW_BORDERLESS;
    if (desc.window_level == WindowLevel::AlwaysOnTop) flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    if (desc.maximized) flags |= SDL_WINDOW_MAXIMIZED;

    auto displays = display_ids();
    if (displays.empty()) {
        throw std::runtime_error("SDL3 reported no displays; cannot create a window");
    }
    if (desc.monitor < 0 || desc.monitor >= static_cast<int>(displays.size())) {
        desc.monitor = 0;  // reset to default monitor
    }
    const SDL_DisplayID display = displays[desc.monitor];
    SDL_Rect bounds{};
    SDL_GetDisplayBounds(display, &bounds);
    const SDL_DisplayMode* desktop_mode = SDL_GetDesktopDisplayMode(display);

    int width  = desc.size.first;
    int height = desc.size.second;
    if (desc.window_mode != WindowMode::Windowed && desktop_mode != nullptr) {
        width  = desktop_mode->w;
        height = desktop_mode->h;
    }

    SDL_Window* window = SDL_CreateWindow(desc.title.c_str(), width, height, flags);
    if (window == nullptr) {
        throw std::runtime_error(std::string("Failed to create SDL window: ") + SDL_GetError());
    }
    spdlog::trace("[sdl3] SDL window handle created for entity {}.", id.index);

    if (desc.window_mode == WindowMode::Windowed) {
        SDL_SetWindowPosition(window, desc.final_pos.first, desc.final_pos.second);
    } else if (desc.window_mode == WindowMode::Fullscreen) {
        apply_exclusive_fullscreen(window, display);
    } else if (desc.window_mode == WindowMode::BorderlessFullscreen) {
        SDL_SetWindowFullscreenMode(window, nullptr);
        SDL_SetWindowFullscreen(window, true);
        SDL_SetWindowPosition(window, bounds.x, bounds.y);
    }

    SDL_SetWindowMinimumSize(window, desc.size_limits.min_width, desc.size_limits.min_height);
    SDL_SetWindowMaximumSize(window, std::max(desc.size_limits.max_width, 0),
                             std::max(desc.size_limits.max_height, 0));
    SDL_SetWindowResizable(window, desc.resizable);
    SDL_SetWindowBordered(window, desc.decorations && desc.window_mode == WindowMode::Windowed);
    SDL_SetWindowAlwaysOnTop(window, desc.window_level == WindowLevel::AlwaysOnTop);
    SDL_SetWindowOpacity(window, desc.opacity);
    if (desc.iconified) SDL_MinimizeWindow(window);
    if (desc.maximized) SDL_MaximizeWindow(window);
    if (desc.focused) SDL_RaiseWindow(window);
    if (desc.visible) SDL_ShowWindow(window);

    apply_cursor_mode(window, desc.cursor_mode);
    SDL_StartTextInput(window);

    SDL_GetWindowSizeInPixels(window, &desc.physical_size.first, &desc.physical_size.second);
    const float scale = SDL_GetWindowDisplayScale(window);
    desc.scale_factor = scale > 0.0f ? scale : 1.0f;
    return window;
}

void SDL3Plugin::update_size(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                             ResMut<SDL3windows> sdl_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = sdl_windows->find(id); it == sdl_windows->end()) {
            continue;  // window not created yet
        }
        auto* window = sdl_windows->at(id);
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (cached.size != desc.size) {
            // Size was requested by a system; issue it and keep the request as
            // the cached value. The window manager reports the size it actually
            // applied through SDL_EVENT_WINDOW_RESIZED, which the next frame
            // reads back - reading immediately would undo a request the window
            // manager has not processed yet, and would be wrong in fullscreen
            // where the size is dictated by the display mode.
            SDL_SetWindowSize(window, desc.size.first, desc.size.second);
            cached.size = desc.size;
            continue;
        }
        int width  = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        desc.size   = {width, height};
        cached.size = desc.size;
        SDL_GetWindowSizeInPixels(window, &desc.physical_size.first, &desc.physical_size.second);
        cached.physical_size = desc.physical_size;
        const float scale    = SDL_GetWindowDisplayScale(window);
        desc.scale_factor    = scale > 0.0f ? scale : 1.0f;
        cached.scale_factor  = desc.scale_factor;
    }
}

void SDL3Plugin::update_pos(Commands commands,
                            Query<Item<Entity, Mut<Window>, Opt<const CachedWindow&>, Opt<const Parent&>>> windows,
                            ResMut<SDL3windows> sdl_windows,
                            Local<std::unordered_map<Entity, std::pair<int, int>>> pending_window_positions) {
    /// For all windows in the query, if it has been cached(created), then if
    /// the pos has been changed manually, e.g. different with cached, then
    /// the new pos should be calculated with new final pos and new pos_type if
    /// it is changed. If pos is changed, then recalculate the final pos
    /// based on the pos_type. Otherwise if pos type changed, final pos should
    /// remain the same and recalculate the pos based on the pos_type.
    /// If it was not cached, then we use the relative pos to calculate the
    /// final pos.
    std::unordered_map<Entity, std::pair<int, int>> final_positions;
    const auto displays = display_ids();
    if (displays.empty()) return;
    std::function<std::pair<int, int>(Entity)> calculate;
    calculate = [&](Entity id) -> std::pair<int, int> {
        if (final_positions.contains(id)) {
            return final_positions.at(id);
        }
        auto [_, mdesc, cached_window, rparent] = windows.get(id).value();
        auto* parent                            = rparent.has_value() ? &rparent.value().get() : nullptr;
        auto& desc                              = mdesc.get_mut();
        SDL_Window* sdl_window                  = nullptr;
        if (auto it = sdl_windows->find(id); it != sdl_windows->end()) {
            sdl_window = it->second;
        }
        if (desc.monitor < 0 || desc.monitor >= static_cast<int>(displays.size())) {
            desc.monitor = 0;  // default to first monitor
        }
        std::optional<std::pair<int, int>> parent_pos;
        if (parent) {
            if (windows.contains(parent->entity()))
                parent_pos = calculate(parent->entity());
            else
                commands.entity(id).remove<Parent>();
        }
        SDL_Rect monitor{};
        SDL_GetDisplayBounds(displays[desc.monitor], &monitor);

        if (sdl_window != nullptr) {
            auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window->get()));
            if (cached.final_pos != desc.final_pos) {
                // calculate the relevant pos based on pos_type
                if (desc.pos_type == PosType::TopLeft) {
                    desc.pos = {desc.final_pos.first - monitor.x, desc.final_pos.second - monitor.y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        desc.pos = {desc.final_pos.first - parent_pos->first,
                                    desc.final_pos.second - parent_pos->second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    desc.pos = {desc.final_pos.first - monitor.x - (monitor.w - desc.size.first) / 2,
                                desc.final_pos.second - monitor.y - (monitor.h - desc.size.second) / 2};
                }
            } else if (cached.pos != desc.pos) {
                // recalculate final pos based on pos_type
                if (desc.pos_type == PosType::TopLeft) {
                    desc.final_pos = {desc.pos.first + monitor.x, desc.pos.second + monitor.y};
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
                    desc.final_pos = {desc.pos.first + monitor.x + (monitor.w - desc.size.first) / 2,
                                      desc.pos.second + monitor.y + (monitor.h - desc.size.second) / 2};
                }
            } else if (cached.pos_type != desc.pos_type) {
                // recalculate pos based on pos_type
                if (desc.pos_type == PosType::TopLeft) {
                    desc.pos = {desc.final_pos.first - monitor.x, desc.final_pos.second - monitor.y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        desc.pos = {desc.final_pos.first - parent_pos->first,
                                    desc.final_pos.second - parent_pos->second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    desc.pos = {desc.final_pos.first - monitor.x - (monitor.w - desc.size.first) / 2,
                                desc.final_pos.second - monitor.y - (monitor.h - desc.size.second) / 2};
                }
            }
            // if this window has a parent, we need to recalculate the final pos
            // anyway
            if (desc.pos_type == PosType::Relative && parent_pos) {
                desc.final_pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
            }
            const bool requested_native_move =
                desc.final_pos != cached.final_pos || desc.pos != cached.pos || desc.pos_type != cached.pos_type;
            if (requested_native_move) {
                if (desc.window_mode == WindowMode::Windowed) {
                    SDL_SetWindowPosition(sdl_window, desc.final_pos.first, desc.final_pos.second);
                }
                cached.final_pos = desc.final_pos;
                cached.pos       = desc.pos;
                cached.pos_type  = desc.pos_type;
                pending_window_positions->insert_or_assign(id, desc.final_pos);
                final_positions[id] = desc.final_pos;
                return desc.final_pos;
            }
            {
                // read from the native window, and guarantee that the ultimate
                // position is correct.
                int actual_x = 0;
                int actual_y = 0;
                SDL_GetWindowPosition(sdl_window, &actual_x, &actual_y);
                if (auto pending = pending_window_positions->find(id); pending != pending_window_positions->end()) {
                    if (pending->second.first != actual_x || pending->second.second != actual_y) {
                        final_positions[id] = pending->second;
                        return pending->second;
                    }
                    pending_window_positions->erase(pending);
                }
                desc.final_pos   = {actual_x, actual_y};
                cached.final_pos = desc.final_pos;
                // calculate the pos based on final_pos
                const auto& final_pos = desc.final_pos;
                std::pair<int, int> pos;
                if (desc.pos_type == PosType::TopLeft) {
                    pos = {final_pos.first - monitor.x, final_pos.second - monitor.y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        pos = {final_pos.first - parent_pos->first, final_pos.second - parent_pos->second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    pos = {final_pos.first - monitor.x - (monitor.w - desc.size.first) / 2,
                           final_pos.second - monitor.y - (monitor.h - desc.size.second) / 2};
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
                desc.final_pos = {desc.pos.first + monitor.x, desc.pos.second + monitor.y};
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
                desc.final_pos = {desc.pos.first + monitor.x + (monitor.w - desc.size.first) / 2,
                                  desc.pos.second + monitor.y + (monitor.h - desc.size.second) / 2};
            }

            final_positions[id] = desc.final_pos;
            return desc.final_pos;
        }
    };
    for (auto&& [id, desc, cached_window, parent] : windows.iter()) {
        calculate(id);
    }
}

void SDL3Plugin::create_windows(Commands cmd,
                                Query<Item<Entity, Mut<Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                                ResMut<SDL3windows> sdl_windows,
                                EventWriter<WindowCreated> window_created) {
    for (auto&& [id, desc, parent, children] : windows.iter()) {
        if (sdl_windows->contains(id)) {
            continue;
        }
        auto* created = create_window(id, desc);
        sdl_windows->emplace(id, created);
        spdlog::debug("[sdl3] Window created for entity {} ('{}').", id.index, desc.get().title);
        window_created.write(WindowCreated{id});
        cmd.entity(id).insert(CachedWindow{desc});
    }
}

void SDL3Plugin::update_window_states(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                                      Res<assets::Assets<image::Image>> images,
                                      ResMut<SDL3windows> sdl_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = sdl_windows->find(id); it == sdl_windows->end()) {
            continue;  // window not created yet
        }
        auto* window = sdl_windows->at(id);
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));

        // resizable
        if (cached.resizable != desc.resizable) {
            SDL_SetWindowResizable(window, desc.resizable);
            cached.resizable = desc.resizable;
        }
        // decorations
        if (cached.decorations != desc.decorations) {
            SDL_SetWindowBordered(window, desc.decorations && desc.window_mode == WindowMode::Windowed);
            cached.decorations = desc.decorations;
        }
        // visible
        if (cached.visible != desc.visible) {
            if (desc.visible)
                SDL_ShowWindow(window);
            else
                SDL_HideWindow(window);
            cached.visible = desc.visible;
        }
        // opacity
        if (cached.opacity != desc.opacity) {
            if (!SDL_SetWindowOpacity(window, desc.opacity)) {
                spdlog::warn("[sdl3] Failed to set window opacity: {}", SDL_GetError());
            }
            cached.opacity = desc.opacity;
        }
        // size limits
        if (cached.size_limits != desc.size_limits) {
            SDL_SetWindowMinimumSize(window, desc.size_limits.min_width, desc.size_limits.min_height);
            SDL_SetWindowMaximumSize(window, std::max(desc.size_limits.max_width, 0),
                                     std::max(desc.size_limits.max_height, 0));
            cached.size_limits = desc.size_limits;
        }
        // title
        if (cached.title != desc.title) {
            SDL_SetWindowTitle(window, desc.title.c_str());
            cached.title = desc.title;
        }

        // request attention
        if (desc.attention_request) SDL_FlashWindow(window, SDL_FLASH_BRIEFLY);
        desc.attention_request   = false;
        cached.attention_request = false;

        // cursor mode
        if (cached.cursor_mode != desc.cursor_mode) {
            apply_cursor_mode(window, desc.cursor_mode);
            cached.cursor_mode = desc.cursor_mode;
        }
        // cursor icon
        if (cached.cursor_icon != desc.cursor_icon) {
            std::visit(utils::visitor{
                           [&](const StandardCursor& icon) {
                               install_cursor(window, SDL_CreateSystemCursor(to_system_cursor(icon)));
                               cached.cursor_icon = desc.cursor_icon;
                           },
                           [&](const CustomCursor& handle) {
                               images->get(handle.image).transform([&](const image::Image& img) {
                                   auto pixels = extract_rgba_pixels(img);
                                   if (!pixels) {
                                       spdlog::warn(
                                           "Image format '{}' is not supported for a cursor or icon; ignoring.",
                                           wgpu::to_string(img.texture_format()));
                                       return std::cref(img);
                                   }
                                   SDL_Surface* surface = SDL_CreateSurfaceFrom(
                                       static_cast<int>(img.width()), static_cast<int>(img.height()),
                                       SDL_PIXELFORMAT_RGBA32, pixels->data(),
                                       static_cast<int>(img.width()) * 4);
                                   if (surface == nullptr) {
                                       spdlog::warn("[sdl3] Failed to build a cursor surface: {}", SDL_GetError());
                                       return std::cref(img);
                                   }
                                   auto* cursor = SDL_CreateColorCursor(surface, static_cast<int>(handle.hot_x),
                                                                        static_cast<int>(handle.hot_y));
                                   SDL_DestroySurface(surface);
                                   install_cursor(window, cursor);
                                   cached.cursor_icon = desc.cursor_icon;
                                   return std::cref(img);
                               });
                           }},
                       desc.cursor_icon);
        }
        // window icon
        if (cached.icon != desc.icon) {
            if (desc.icon.has_value()) {
                images->get(desc.icon.value()).transform([&](const image::Image& img) {
                    auto pixels = extract_rgba_pixels(img);
                    if (!pixels) return std::cref(img);
                    SDL_Surface* surface =
                        SDL_CreateSurfaceFrom(static_cast<int>(img.width()), static_cast<int>(img.height()),
                                              SDL_PIXELFORMAT_RGBA32, pixels->data(), static_cast<int>(img.width()) * 4);
                    if (surface != nullptr) {
                        SDL_SetWindowIcon(window, surface);
                        SDL_DestroySurface(surface);
                        cached.icon = desc.icon;
                    }
                    return std::cref(img);
                });
            } else {
                // SDL has no "reset to default icon" call; the icon stays.
                cached.icon = desc.icon;
            }
        }

        // frame size
        SDL_GetWindowBordersSize(window, &desc.frame_size.top, &desc.frame_size.left, &desc.frame_size.bottom,
                                 &desc.frame_size.right);
        cached.frame_size = desc.frame_size;

        // cursor position: a written value is applied, otherwise SDL reports the
        // position. It is passed through as-is, so it may lie outside the client
        // area once the cursor leaves the window; clamping is the caller's
        // business.
        if (cached.cursor_pos != desc.cursor_pos) {
            SDL_WarpMouseInWindow(window, static_cast<float>(desc.cursor_pos.first),
                                  static_cast<float>(desc.cursor_pos.second));
            cached.cursor_pos = desc.cursor_pos;
        } else {
            float x = 0.0f;
            float y = 0.0f;
            SDL_GetMouseState(&x, &y);
            desc.cursor_pos   = {x, y};
            cached.cursor_pos = desc.cursor_pos;
        }
        // cursor in window
        desc.cursor_in_window   = (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
        cached.cursor_in_window = desc.cursor_in_window;
        // maximized
        if (cached.maximized != desc.maximized) {
            if (desc.maximized) {
                SDL_MaximizeWindow(window);
            } else {
                SDL_RestoreWindow(window);
            }
            cached.maximized = desc.maximized;
        }
        // iconified
        if (cached.iconified != desc.iconified) {
            if (desc.iconified) {
                SDL_MinimizeWindow(window);
            } else {
                SDL_RestoreWindow(window);
            }
            cached.iconified = desc.iconified;
        }
        {
            const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
            desc.maximized              = (flags & SDL_WINDOW_MAXIMIZED) != 0;
            desc.iconified              = (flags & SDL_WINDOW_MINIMIZED) != 0;
            cached.maximized            = desc.maximized;
            cached.iconified            = desc.iconified;
        }
        // window level: SDL only models "always on top", so AlwaysOnBottom is
        // reported as Normal.
        if (cached.window_level != desc.window_level) {
            SDL_SetWindowAlwaysOnTop(window, desc.window_level == WindowLevel::AlwaysOnTop);
            cached.window_level = desc.window_level;
        } else {
            desc.window_level =
                (SDL_GetWindowFlags(window) & SDL_WINDOW_ALWAYS_ON_TOP) != 0 ? WindowLevel::AlwaysOnTop
                                                                            : WindowLevel::Normal;
            cached.window_level = desc.window_level;
        }

        // properties SDL does not apply itself; the render system reads them.
        cached.composite_alpha_mode = desc.composite_alpha_mode;
        cached.present_mode         = desc.present_mode;
    }
    // focused will be handled here, cause changing one window's focus will
    // change the focus of all windows.
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc   = mdesc.get();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (auto it = sdl_windows->find(id); it != sdl_windows->end()) {
            auto* window = it->second;
            if (cached.focused != desc.focused && desc.focused) {
                SDL_RaiseWindow(window);
                cached.focused = desc.focused;
            }
        }
    }
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc   = mdesc.get_mut();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (auto it = sdl_windows->find(id); it != sdl_windows->end()) {
            auto* window = it->second;
            // read focused state from the native window
            desc.focused   = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
            cached.focused = desc.focused;
            // SDL only applies relative mouse mode to the keyboard-focused
            // window, so a request made while another window held focus was
            // dropped. The mode is a property of the component, so it has to be
            // re-asserted once focus arrives.
            if (desc.cursor_mode == CursorMode::Disabled && desc.focused &&
                !SDL_GetWindowRelativeMouseMode(window)) {
                apply_cursor_mode(window, CursorMode::Disabled);
            }
        }
    }
}

void SDL3Plugin::toggle_window_mode(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                                    ResMut<SDL3windows> sdl_windows,
                                    Local<std::unordered_map<Entity, CachedWindowPosSize>> cached_window_sizes) {
    const auto displays = display_ids();
    if (displays.empty()) return;
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = sdl_windows->find(id); it == sdl_windows->end()) {
            // not created, but when created, it will automatically
            // set the window mode based on the description. so we need to
            // store the cached size now.
            if (desc.window_mode != WindowMode::Windowed) {
                (*cached_window_sizes)[id] =
                    CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first, desc.size.second};
            }
            continue;  // window not created yet
        }
        auto* window = sdl_windows->at(id);
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (desc.monitor < 0 || desc.monitor >= static_cast<int>(displays.size())) {
            desc.monitor = 0;
        }
        const SDL_DisplayID display = displays[desc.monitor];
        SDL_Rect bounds{};
        SDL_GetDisplayBounds(display, &bounds);
        if (desc.window_mode != cached.window_mode) {
            if (desc.window_mode == WindowMode::Windowed) {
                SDL_SetWindowFullscreen(window, false);
                SDL_SetWindowBordered(window, desc.decorations);
                if (auto cached_size = cached_window_sizes->find(id); cached_size != cached_window_sizes->end()) {
                    SDL_SetWindowPosition(window, cached_size->second.pos_x, cached_size->second.pos_y);
                    SDL_SetWindowSize(window, cached_size->second.width, cached_size->second.height);
                    cached_window_sizes->erase(cached_size);
                } else {
                    SDL_SetWindowPosition(window, 100, 100);
                    SDL_SetWindowSize(window, 1280, 720);
                }
            } else if (desc.window_mode == WindowMode::Fullscreen) {
                cached_window_sizes->insert_or_assign(
                    id, CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first,
                                            desc.size.second});
                apply_exclusive_fullscreen(window, display);
            } else if (desc.window_mode == WindowMode::BorderlessFullscreen) {
                cached_window_sizes->insert_or_assign(
                    id, CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first,
                                            desc.size.second});
                SDL_SetWindowFullscreenMode(window, nullptr);
                SDL_SetWindowFullscreen(window, true);
                SDL_SetWindowBordered(window, false);
                SDL_SetWindowPosition(window, bounds.x, bounds.y);
            }
        } else if (cached.monitor != desc.monitor && desc.window_mode != WindowMode::Windowed) {
            // monitor changed, fullscreen or borderless mode
            // should set to the new monitor
            if (desc.window_mode == WindowMode::Fullscreen) {
                apply_exclusive_fullscreen(window, display);
            } else {
                SDL_SetWindowFullscreenMode(window, nullptr);
                SDL_SetWindowFullscreen(window, true);
                SDL_SetWindowBordered(window, false);
                SDL_SetWindowPosition(window, bounds.x, bounds.y);
            }
            cached_window_sizes->insert_or_assign(
                id, CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first, desc.size.second});
        }
        cached.window_mode = desc.window_mode;
        // we wont change cached.monitor here cause it still needs to be
        // used to calculate the final position of the window.
    }
}

void SDL3Plugin::send_cached_events(Res<SDL3Events> events,
                                    ResMut<SDL3windows> sdl_windows,
                                    EventWriter<window::WindowResized> window_resized,
                                    EventWriter<window::WindowScaleFactorChanged> window_scale_factor_changed,
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
                                    std::optional<EventWriter<input::MouseScroll>> scroll_input,
                                    Local<std::pair<Entity, std::vector<std::string>>> pending_drop) {
    // SDL reports a drop as DROP_BEGIN, one DROP_FILE per file, then
    // DROP_COMPLETE, while `window::FileDrop` carries every path of one drop in
    // a single event - so the paths are collected and flushed together.
    auto flush_drop = [&] {
        if (pending_drop->second.empty()) return;
        file_drop.write(FileDrop{pending_drop->first, std::move(pending_drop->second)});
        pending_drop->second.clear();
    };
    for (std::size_t offset = 0; offset + sizeof(SDL_Event) <= events->events.size(); offset += sizeof(SDL_Event)) {
        SDL_Event event;
        std::memcpy(&event, events->events.data() + offset, sizeof(SDL_Event));
        switch (event.type) {
            case SDL_EVENT_QUIT:
                spdlog::debug("[sdl3] SDL requested application quit; window close events drive the exit condition.");
                break;
            case SDL_EVENT_WINDOW_RESIZED: {
                if (auto id = entity_of_window_id(*sdl_windows, event.window.windowID)) {
                    window_resized.write(WindowResized{*id, event.window.data1, event.window.data2});
                }
                break;
            }
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
                auto id = entity_of_window_id(*sdl_windows, event.window.windowID);
                if (!id) break;
                if (SDL_Window* native = SDL_GetWindowFromID(event.window.windowID)) {
                    const float scale = SDL_GetWindowDisplayScale(native);
                    window_scale_factor_changed.write(WindowScaleFactorChanged{*id, scale > 0.0f ? scale : 1.0f});
                }
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                if (auto id = entity_of_window_id(*sdl_windows, event.window.windowID)) {
                    window_close_requested.write(WindowCloseRequested{*id});
                }
                break;
            }
            case SDL_EVENT_WINDOW_MOVED: {
                if (auto id = entity_of_window_id(*sdl_windows, event.window.windowID)) {
                    window_moved.write(WindowMoved{*id, {event.window.data1, event.window.data2}});
                }
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST: {
                if (auto id = entity_of_window_id(*sdl_windows, event.window.windowID)) {
                    window_focused.write(WindowFocused{*id, event.type == SDL_EVENT_WINDOW_FOCUS_GAINED});
                }
                break;
            }
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
                if (auto id = entity_of_window_id(*sdl_windows, event.window.windowID)) {
                    cursor_entered.write(CursorEntered{*id, event.type == SDL_EVENT_WINDOW_MOUSE_ENTER});
                }
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (!key_input_event) break;
                if (auto id = entity_of_window_id(*sdl_windows, event.key.windowID)) {
                    key_input_event->write(input::KeyInput{map_sdl3_key_to_input(event.key.key),
                                                           static_cast<int>(event.key.scancode), event.key.down,
                                                           event.key.repeat, *id});
                }
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                auto id = entity_of_window_id(*sdl_windows, event.button.windowID);
                if (!id) break;
                if (mouse_button_input) {
                    mouse_button_input->write(input::MouseButtonInput{
                        map_sdl3_mouse_button_to_input(event.button.button), event.button.down, *id});
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                auto id = entity_of_window_id(*sdl_windows, event.motion.windowID);
                if (!id) break;
                const std::pair<double, double> delta{event.motion.xrel, event.motion.yrel};
                cursor_moved.write(CursorMoved{*id, {event.motion.x, event.motion.y}, delta});
                if (mouse_move_input) mouse_move_input->write(input::MouseMove{delta});
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                auto id = entity_of_window_id(*sdl_windows, event.wheel.windowID);
                if (!id) break;
                const double flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0 : 1.0;
                if (scroll_input) {
                    scroll_input->write(input::MouseScroll{event.wheel.x * flip, event.wheel.y * flip, *id});
                }
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                auto id = entity_of_window_id(*sdl_windows, event.text.windowID);
                if (!id) break;
                for_each_codepoint(event.text.text, [&](char32_t code) {
                    received_character.write(window::ReceivedCharacter{*id, code});
                });
                break;
            }
            case SDL_EVENT_DROP_BEGIN:
                // A new drop supersedes anything still pending.
                flush_drop();
                pending_drop->first = entity_of_window_id(*sdl_windows, event.drop.windowID).value_or(Entity{});
                break;
            case SDL_EVENT_DROP_FILE: {
                auto id = entity_of_window_id(*sdl_windows, event.drop.windowID);
                if (!id) break;
                if (pending_drop->second.empty()) pending_drop->first = *id;
                if (event.drop.data != nullptr) pending_drop->second.emplace_back(event.drop.data);
                break;
            }
            case SDL_EVENT_DROP_COMPLETE:
                flush_drop();
                break;
            default:
                break;
        }
    }
}

void SDL3Plugin::destroy_windows(Query<Item<Entity, const Window&>> windows,
                                 ResMut<SDL3windows> sdl_windows,
                                 EventWriter<WindowClosed> window_closed,
                                 EventWriter<WindowDestroyed> window_destroyed) {
    std::unordered_set<Entity> still_alive;
    for (auto&& [id, window_desc] : windows.iter()) {
        still_alive.insert(id);
    }
    std::vector<Entity> to_erase;
    for (auto&& [id, sdl_window] : std::views::filter(std::views::all(*sdl_windows), [&](auto&& tuple) {
             auto&& [id, _] = tuple;
             return !still_alive.contains(id);
         })) {
        window_closed.write(WindowClosed{id});
        release_cursor(sdl_window);
        SDL_DestroyWindow(sdl_window);
        spdlog::debug("[sdl3] Window entity {} destroyed.", id.index);
        to_erase.emplace_back(id);
        window_destroyed.write(WindowDestroyed{id});
    }
    for (auto&& id : to_erase) {
        sdl_windows->erase(id);
    }
}

void SDL3Plugin::destroy_all_windows(ResMut<SDL3windows> sdl_windows,
                                     EventWriter<WindowClosed> window_closed,
                                     EventWriter<WindowDestroyed> window_destroyed) {
    for (auto&& [id, sdl_window] : *sdl_windows) {
        window_closed.write(WindowClosed{id});
        release_cursor(sdl_window);
        SDL_DestroyWindow(sdl_window);
        window_destroyed.write(WindowDestroyed{id});
    }
    sdl_windows->clear();
}

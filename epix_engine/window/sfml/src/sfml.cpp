module;

#include <SFML/Window/Clipboard.hpp>
#include <SFML/Window/Cursor.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>
#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/WindowBase.hpp>
#include <SFML/Window/WindowEnums.hpp>
#include <SFML/Window/WindowHandle.hpp>

module epix.sfml.core;

import epix.utils;

using namespace sfml;
using namespace window;

const std::string& Clipboard::get_text() const { return text; }
void Clipboard::update(ResMut<Clipboard> clipboard) {
    sf::String str  = sf::Clipboard::getString();
    clipboard->text = str.toAnsiString();
}
void Clipboard::set_text(EventReader<SetClipboardString> events) {
    for (auto&& event : events.read()) {
        sf::Clipboard::setString(sf::String(event.text));
    }
}

std::shared_ptr<sf::WindowBase> SFMLPlugin::create_window(Entity id, Window& desc) {
    auto [width, height] = desc.size;

    std::uint32_t style = sf::Style::None;
    if (desc.decorations) {
        style |= sf::Style::Titlebar | sf::Style::Close;
    }
    if (desc.resizable) {
        style |= sf::Style::Resize;
    }

    sf::State state = sf::State::Windowed;
    if (desc.window_mode == WindowMode::Fullscreen || desc.window_mode == WindowMode::BorderlessFullscreen) {
        state = sf::State::Fullscreen;
    }

    auto window =
        std::make_shared<sf::WindowBase>(sf::VideoMode({static_cast<unsigned>(width), static_cast<unsigned>(height)}),
                                         sf::String(desc.title), style, state);

    if (desc.window_mode == WindowMode::Windowed) {
        window->setPosition({desc.final_pos.first, desc.final_pos.second});
    }

    // size limits
    if (desc.size_limits.min_width > 0 && desc.size_limits.min_height > 0) {
        window->setMinimumSize(sf::Vector2u{static_cast<unsigned>(desc.size_limits.min_width),
                                            static_cast<unsigned>(desc.size_limits.min_height)});
    }
    if (desc.size_limits.max_width > 0 && desc.size_limits.max_height > 0) {
        window->setMaximumSize(sf::Vector2u{static_cast<unsigned>(desc.size_limits.max_width),
                                            static_cast<unsigned>(desc.size_limits.max_height)});
    }

    window->setVisible(desc.visible);

    // cursor mode
    if (desc.cursor_mode == CursorMode::Hidden || desc.cursor_mode == CursorMode::Disabled) {
        window->setMouseCursorVisible(false);
    }
    if (desc.cursor_mode == CursorMode::Captured) {
        window->setMouseCursorGrabbed(true);
    }

    return window;
}

void SFMLPlugin::update_size(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                             ResMut<SFMLwindows> sfml_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) {
            continue;
        }
        auto* window = sfml_windows->at(id).get();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (cached.size != desc.size) {
            window->setSize({static_cast<unsigned>(desc.size.first), static_cast<unsigned>(desc.size.second)});
        }
        auto sz     = window->getSize();
        desc.size   = {static_cast<int>(sz.x), static_cast<int>(sz.y)};
        cached.size = desc.size;
    }
}

void SFMLPlugin::update_pos(Commands commands,
                            Query<Item<Entity, Mut<Window>, Opt<const CachedWindow&>, Opt<const Parent&>>> windows,
                            ResMut<SFMLwindows> sfml_windows,
                            EventWriter<WindowMoved> window_moved) {
    std::unordered_map<Entity, std::pair<int, int>> final_positions;

    // Record cached positions from previous frame to detect user-initiated moves
    std::unordered_map<Entity, std::pair<int, int>> old_positions;
    for (auto&& [id, desc, cached_window, parent] : windows.iter()) {
        if (cached_window.has_value()) {
            auto& cached      = *reinterpret_cast<const Window*>(&cached_window->get());
            old_positions[id] = cached.final_pos;
        }
    }

    auto desktop_mode = sf::VideoMode::getDesktopMode();
    int desktop_w     = static_cast<int>(desktop_mode.size.x);
    int desktop_h     = static_cast<int>(desktop_mode.size.y);

    std::function<std::pair<int, int>(Entity)> calculate;
    calculate = [&](Entity id) -> std::pair<int, int> {
        if (final_positions.contains(id)) {
            return final_positions.at(id);
        }
        auto [_, mdesc, cached_window, rparent] = windows.get(id).value();
        auto* parent                            = rparent.has_value() ? &rparent.value().get() : nullptr;
        auto& desc                              = mdesc.get_mut();
        sf::WindowBase* sfml_window             = nullptr;
        if (auto it = sfml_windows->find(id); it != sfml_windows->end()) {
            sfml_window = it->second.get();
        }

        // SFML doesn't have multi-monitor API, use monitor 0 / desktop
        int monitor_x = 0, monitor_y = 0;
        int monitor_w = desktop_w, monitor_h = desktop_h;

        std::optional<std::pair<int, int>> parent_pos;
        if (parent) {
            if (windows.contains(parent->entity()))
                parent_pos = calculate(parent->entity());
            else
                commands.entity(id).remove<Parent>();
        }

        if (sfml_window != nullptr) {
            auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window->get()));
            if (cached.final_pos != desc.final_pos) {
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
                    desc.pos = {desc.final_pos.first - monitor_x - (monitor_w - desc.size.first) / 2,
                                desc.final_pos.second - monitor_y - (monitor_h - desc.size.second) / 2};
                }
            } else if (cached.pos != desc.pos) {
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
                    desc.final_pos = {desc.pos.first + monitor_x + (monitor_w - desc.size.first) / 2,
                                      desc.pos.second + monitor_y + (monitor_h - desc.size.second) / 2};
                }
            } else if (cached.pos_type != desc.pos_type) {
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
                    desc.pos = {desc.final_pos.first - (monitor_w - desc.size.first) / 2,
                                desc.final_pos.second - (monitor_h - desc.size.second) / 2};
                }
            }
            if (desc.pos_type == PosType::Relative && parent_pos) {
                desc.final_pos = {parent_pos->first + desc.pos.first, parent_pos->second + desc.pos.second};
            }
            if (desc.final_pos != cached.final_pos || desc.pos != cached.pos || desc.pos_type != cached.pos_type) {
                sfml_window->setPosition({desc.final_pos.first, desc.final_pos.second});
                cached.final_pos = desc.final_pos;
                cached.pos       = desc.pos;
                cached.pos_type  = desc.pos_type;
            }
            {
                auto pos              = sfml_window->getPosition();
                desc.final_pos        = {pos.x, pos.y};
                cached.final_pos      = desc.final_pos;
                const auto& final_pos = desc.final_pos;
                std::pair<int, int> rpos;
                if (desc.pos_type == PosType::TopLeft) {
                    rpos = {final_pos.first - monitor_x, final_pos.second - monitor_y};
                }
                if (desc.pos_type == PosType::Relative) {
                    if (parent_pos) {
                        rpos = {final_pos.first - parent_pos->first, final_pos.second - parent_pos->second};
                    } else {
                        desc.pos_type = PosType::Centered;
                    }
                }
                if (desc.pos_type == PosType::Centered) {
                    rpos = {final_pos.first - (monitor_w - desc.size.first) / 2,
                            final_pos.second - (monitor_h - desc.size.second) / 2};
                }
                desc.pos            = rpos;
                cached.final_pos    = desc.final_pos;
                cached.pos          = desc.pos;
                cached.pos_type     = desc.pos_type;
                final_positions[id] = desc.final_pos;
                return desc.final_pos;
            }
        } else {
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
                desc.final_pos = {desc.pos.first + monitor_x + (monitor_w - desc.size.first) / 2,
                                  desc.pos.second + monitor_y + (monitor_h - desc.size.second) / 2};
            }
            final_positions[id] = desc.final_pos;
            return desc.final_pos;
        }
    };
    // Record which window currently has focus before repositioning
    Entity focused_window_id{};
    bool has_focused_window = false;
    for (auto&& [id, window_ptr] : *sfml_windows) {
        if (window_ptr && window_ptr->hasFocus()) {
            focused_window_id  = id;
            has_focused_window = true;
            break;
        }
    }

    for (auto&& [id, desc, cached_window, parent] : windows.iter()) {
        calculate(id);
    }

    // Emit WindowMoved events for windows whose position changed
    for (auto&& [id, window_ptr] : *sfml_windows) {
        if (window_ptr) {
            auto pos = window_ptr->getPosition();
            if (auto it = old_positions.find(id); it != old_positions.end()) {
                if (it->second.first != pos.x || it->second.second != pos.y) {
                    window_moved.write(WindowMoved{id, {pos.x, pos.y}});
                }
            }
        }
    }

    // Restore focus to the window that had it before repositioning
    if (has_focused_window) {
        if (auto it = sfml_windows->find(focused_window_id); it != sfml_windows->end() && it->second) {
            if (!it->second->hasFocus()) {
                it->second->requestFocus();
            }
        }
    }
}

void SFMLPlugin::create_windows(Commands cmd,
                                Query<Item<Entity, Mut<Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                                ResMut<SFMLwindows> sfml_windows,
                                EventWriter<WindowCreated> window_created) {
    for (auto&& [id, desc, parent, children] : windows.iter()) {
        if (sfml_windows->contains(id)) {
            continue;
        }
        auto created = create_window(id, desc);
        sfml_windows->emplace(id, std::move(created));
        window_created.write(WindowCreated{id});
        cmd.entity(id).insert(CachedWindow{desc});
    }
}

void SFMLPlugin::update_window_states(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                                      Res<assets::Assets<image::Image>> images,
                                      ResMut<SFMLwindows> sfml_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) {
            continue;
        }
        auto* window = sfml_windows->at(id).get();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));

        // visible
        if (cached.visible != desc.visible) {
            window->setVisible(desc.visible);
            cached.visible = desc.visible;
        }
        // size limits
        if (cached.size_limits != desc.size_limits) {
            if (desc.size_limits.min_width > 0 && desc.size_limits.min_height > 0) {
                window->setMinimumSize(sf::Vector2u{static_cast<unsigned>(desc.size_limits.min_width),
                                                    static_cast<unsigned>(desc.size_limits.min_height)});
            } else {
                window->setMinimumSize(std::nullopt);
            }
            if (desc.size_limits.max_width > 0 && desc.size_limits.max_height > 0) {
                window->setMaximumSize(sf::Vector2u{static_cast<unsigned>(desc.size_limits.max_width),
                                                    static_cast<unsigned>(desc.size_limits.max_height)});
            } else {
                window->setMaximumSize(std::nullopt);
            }
            cached.size_limits = desc.size_limits;
        }
        // title
        if (cached.title != desc.title) {
            window->setTitle(sf::String(desc.title));
            cached.title = desc.title;
        }

        // request attention
        if (desc.attention_request) window->requestFocus();
        desc.attention_request   = false;
        cached.attention_request = false;

        // cursor mode
        if (cached.cursor_mode != desc.cursor_mode) {
            switch (desc.cursor_mode) {
                case CursorMode::Normal:
                    window->setMouseCursorVisible(true);
                    window->setMouseCursorGrabbed(false);
                    break;
                case CursorMode::Hidden:
                    window->setMouseCursorVisible(false);
                    window->setMouseCursorGrabbed(false);
                    break;
                case CursorMode::Captured:
                    window->setMouseCursorVisible(true);
                    window->setMouseCursorGrabbed(true);
                    break;
                case CursorMode::Disabled:
                    window->setMouseCursorVisible(false);
                    window->setMouseCursorGrabbed(true);
                    break;
            }
            cached.cursor_mode = desc.cursor_mode;
        }
        // cursor icon
        if (cached.cursor_icon != desc.cursor_icon) {
            std::visit(utils::visitor{[&](const StandardCursor& icon) {
                                          auto type = [icon] {
                                              switch (icon) {
                                                  case StandardCursor::Arrow:
                                                      return sf::Cursor::Type::Arrow;
                                                  case StandardCursor::IBeam:
                                                      return sf::Cursor::Type::Text;
                                                  case StandardCursor::Crosshair:
                                                      return sf::Cursor::Type::Cross;
                                                  case StandardCursor::Hand:
                                                      return sf::Cursor::Type::Hand;
                                                  case StandardCursor::ResizeAll:
                                                      return sf::Cursor::Type::SizeAll;
                                                  case StandardCursor::ResizeNS:
                                                      return sf::Cursor::Type::SizeVertical;
                                                  case StandardCursor::ResizeEW:
                                                      return sf::Cursor::Type::SizeHorizontal;
                                                  case StandardCursor::ResizeNWSE:
                                                      return sf::Cursor::Type::SizeTopLeftBottomRight;
                                                  case StandardCursor::ResizeNESW:
                                                      return sf::Cursor::Type::SizeBottomLeftTopRight;
                                                  case StandardCursor::NotAllowed:
                                                      return sf::Cursor::Type::NotAllowed;
                                                  default:
                                                      return sf::Cursor::Type::Arrow;
                                              }
                                          }();
                                          auto cursor = sf::Cursor::createFromSystem(type);
                                          if (cursor) {
                                              window->setMouseCursor(*cursor);
                                          }
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
                                              auto cursor = sf::Cursor::createFromPixels(
                                                  reinterpret_cast<const std::uint8_t*>(view.data()),
                                                  {static_cast<unsigned>(image.width()),
                                                   static_cast<unsigned>(image.height())},
                                                  {handle.hot_x, handle.hot_y});
                                              if (cursor) {
                                                  window->setMouseCursor(*cursor);
                                              }
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
                    auto view   = image.raw_view();
                    window->setIcon({static_cast<unsigned>(image.width()), static_cast<unsigned>(image.height())},
                                    reinterpret_cast<const std::uint8_t*>(view.data()));
                    cached.icon = desc.icon;
                    return std::cref(img);
                });
            } else {
                cached.icon = desc.icon;
            }
        }

        // cursor position (read from SFML)
        auto mpos         = sf::Mouse::getPosition(*window);
        desc.cursor_pos   = {static_cast<double>(mpos.x), static_cast<double>(mpos.y)};
        cached.cursor_pos = desc.cursor_pos;

        // focused
        desc.focused   = window->hasFocus();
        cached.focused = desc.focused;

        // other none sfml handled properties
        cached.composite_alpha_mode = desc.composite_alpha_mode;
        cached.present_mode         = desc.present_mode;
        cached.resizable            = desc.resizable;
        cached.decorations          = desc.decorations;
        cached.opacity              = desc.opacity;
        cached.window_level         = desc.window_level;
        cached.maximized            = desc.maximized;
        cached.iconified            = desc.iconified;
        cached.frame_size           = desc.frame_size;
    }
}

void SFMLPlugin::toggle_window_mode(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                                    ResMut<SFMLwindows> sfml_windows,
                                    Local<std::unordered_map<Entity, CachedWindowPosSize>> cached_window_sizes) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        auto& desc = mdesc.get_mut();
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) {
            if (desc.window_mode != WindowMode::Windowed) {
                (*cached_window_sizes)[id] =
                    CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first, desc.size.second};
            }
            continue;
        }
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (desc.window_mode != cached.window_mode) {
            // SFML requires recreating the window for fullscreen toggle
            auto* window = sfml_windows->at(id).get();
            if (desc.window_mode == WindowMode::Windowed) {
                std::uint32_t style = sf::Style::Default;
                if (!desc.decorations) style = sf::Style::None;
                auto w = cached_window_sizes->contains(id) ? (*cached_window_sizes)[id].width : desc.size.first;
                auto h = cached_window_sizes->contains(id) ? (*cached_window_sizes)[id].height : desc.size.second;
                window->create(sf::VideoMode({static_cast<unsigned>(w), static_cast<unsigned>(h)}),
                               sf::String(desc.title), style, sf::State::Windowed);
                if (cached_window_sizes->contains(id)) {
                    auto& cs = (*cached_window_sizes)[id];
                    window->setPosition({cs.pos_x, cs.pos_y});
                    cached_window_sizes->erase(id);
                }
            } else {
                // Save current pos/size before going fullscreen
                (*cached_window_sizes)[id] =
                    CachedWindowPosSize{desc.final_pos.first, desc.final_pos.second, desc.size.first, desc.size.second};
                auto desktop = sf::VideoMode::getDesktopMode();
                window->create(desktop, sf::String(desc.title), sf::Style::None, sf::State::Fullscreen);
            }
        }
        cached.window_mode = desc.window_mode;
    }
}

void SFMLPlugin::poll_and_send_events(Query<Item<const CachedWindow&>> cached_windows,
                                      ResMut<SFMLwindows> sfml_windows,
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
    for (auto&& [id, window_ptr] : *sfml_windows) {
        auto* window = window_ptr.get();
        if (!window) continue;

        auto cache_opt                        = cached_windows.get(id);
        std::pair<double, double> last_cursor = {0.0, 0.0};
        if (cache_opt.has_value()) {
            auto&& [cached_window] = cache_opt.value();
            auto& cached           = *reinterpret_cast<const Window*>(&cached_window);
            last_cursor            = cached.cursor_pos;
        }

        while (auto event = window->pollEvent()) {
            event->visit([&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, sf::Event::Closed>) {
                    window_close_requested.write(WindowCloseRequested{id});
                } else if constexpr (std::is_same_v<T, sf::Event::Resized>) {
                    window_resized.write(WindowResized{id, static_cast<int>(e.size.x), static_cast<int>(e.size.y)});
                } else if constexpr (std::is_same_v<T, sf::Event::FocusGained>) {
                    window_focused.write(WindowFocused{id, true});
                } else if constexpr (std::is_same_v<T, sf::Event::FocusLost>) {
                    window_focused.write(WindowFocused{id, false});
                } else if constexpr (std::is_same_v<T, sf::Event::TextEntered>) {
                    received_character.write(window::ReceivedCharacter{id, e.unicode});
                } else if constexpr (std::is_same_v<T, sf::Event::KeyPressed>) {
                    if (key_input_event) {
                        key_input_event->write(input::KeyInput{map_sfml_key_to_input(e.code),
                                                               static_cast<int>(e.scancode), true, false, id});
                    }
                } else if constexpr (std::is_same_v<T, sf::Event::KeyReleased>) {
                    if (key_input_event) {
                        key_input_event->write(input::KeyInput{map_sfml_key_to_input(e.code),
                                                               static_cast<int>(e.scancode), false, false, id});
                    }
                } else if constexpr (std::is_same_v<T, sf::Event::MouseMoved>) {
                    double new_x = static_cast<double>(e.position.x);
                    double new_y = static_cast<double>(e.position.y);
                    cursor_moved.write(
                        CursorMoved{id, {new_x, new_y}, {new_x - last_cursor.first, new_y - last_cursor.second}});
                    if (mouse_move_input) {
                        mouse_move_input->write(
                            input::MouseMove{{new_x - last_cursor.first, new_y - last_cursor.second}});
                    }
                    last_cursor = {new_x, new_y};
                } else if constexpr (std::is_same_v<T, sf::Event::MouseEntered>) {
                    cursor_entered.write(CursorEntered{id, true});
                } else if constexpr (std::is_same_v<T, sf::Event::MouseLeft>) {
                    cursor_entered.write(CursorEntered{id, false});
                } else if constexpr (std::is_same_v<T, sf::Event::MouseButtonPressed>) {
                    if (mouse_button_input) {
                        mouse_button_input->write(
                            input::MouseButtonInput{map_sfml_mouse_button_to_input(e.button), true, id});
                    }
                } else if constexpr (std::is_same_v<T, sf::Event::MouseButtonReleased>) {
                    if (mouse_button_input) {
                        mouse_button_input->write(
                            input::MouseButtonInput{map_sfml_mouse_button_to_input(e.button), false, id});
                    }
                } else if constexpr (std::is_same_v<T, sf::Event::MouseWheelScrolled>) {
                    if (scroll_input) {
                        double xoffset = 0.0, yoffset = 0.0;
                        if (e.wheel == sf::Mouse::Wheel::Vertical) {
                            yoffset = static_cast<double>(e.delta);
                        } else {
                            xoffset = static_cast<double>(e.delta);
                        }
                        scroll_input->write(input::MouseScroll{xoffset, yoffset, id});
                    }
                }
            });
        }
    }
}

void SFMLPlugin::destroy_windows(Query<Item<Entity, const Window&>> windows,
                                 ResMut<SFMLwindows> sfml_windows,
                                 EventWriter<WindowClosed> window_closed,
                                 EventWriter<WindowDestroyed> window_destroyed) {
    std::unordered_set<Entity> still_alive;
    for (auto&& [id, window_desc] : windows.iter()) {
        still_alive.insert(id);
    }
    std::vector<Entity> to_erase;
    for (auto&& [id, sfml_window] : std::views::all(*sfml_windows) | std::views::filter([&](auto&& tuple) {
                                        auto&& [id, _] = tuple;
                                        return !still_alive.contains(id);
                                    })) {
        window_closed.write(WindowClosed{id});
        sfml_window.reset();
        to_erase.emplace_back(id);
        window_destroyed.write(WindowDestroyed{id});
    }
    for (auto&& id : to_erase) {
        sfml_windows->erase(id);
    }
}

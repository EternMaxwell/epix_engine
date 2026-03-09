module;

#include <SFML/Window.hpp>
#include <SFML/Window/Clipboard.hpp>
#include <spdlog/spdlog.h>

module epix.sfml.core;

import std;

using namespace sfml;
using namespace window;
using namespace core;

template <typename... Ts>
struct visitor : Ts... {
    using Ts::operator()...;
};

const std::string& Clipboard::get_text() const { return text; }
void Clipboard::update(ResMut<Clipboard> clipboard) { clipboard->text = sf::Clipboard::getString().toAnsiString(); }
void Clipboard::set_text(EventReader<SetClipboardString> events) {
    for (auto&& event : events.read()) {
        sf::Clipboard::setString(sf::String::fromUtf8(event.text.begin(), event.text.end()));
    }
}

std::unique_ptr<sf::Window> SFMLPlugin::create_window(Entity, Window& desc) {
    auto [width, height] = desc.size;
    sf::Uint32 style     = desc.decorations ? sf::Style::Default : sf::Style::None;
    if (desc.window_mode != WindowMode::Windowed) {
        style = sf::Style::Fullscreen;
    }
    auto window = std::make_unique<sf::Window>(sf::VideoMode(width, height),
                                               sf::String::fromUtf8(desc.title.begin(), desc.title.end()), style);
    window->setPosition({desc.final_pos.first, desc.final_pos.second});
    window->setVisible(desc.visible);
    window->setMouseCursorVisible(desc.cursor_mode == CursorMode::Normal);
    if (desc.focused) {
        window->requestFocus();
    }
    return window;
}

void SFMLPlugin::update_size(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                             ResMut<SFMLwindows> sfml_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) continue;
        auto& desc   = mdesc.get_mut();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        auto* window = it->second.get();
        if (cached.size != desc.size) {
            window->setSize({static_cast<unsigned int>(desc.size.first), static_cast<unsigned int>(desc.size.second)});
        }
        const auto size = window->getSize();
        desc.size       = {static_cast<int>(size.x), static_cast<int>(size.y)};
        cached.size     = desc.size;
    }
}

void SFMLPlugin::update_pos(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                            ResMut<SFMLwindows> sfml_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) continue;
        auto& desc   = mdesc.get_mut();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (cached.final_pos != desc.final_pos) {
            it->second->setPosition({desc.final_pos.first, desc.final_pos.second});
        }
        const auto pos = it->second->getPosition();
        desc.final_pos = {pos.x, pos.y};
        desc.pos       = desc.final_pos;
        cached.final_pos = desc.final_pos;
        cached.pos       = desc.pos;
    }
}

void SFMLPlugin::create_windows(Commands cmd,
                                Query<Item<Entity, Mut<Window>, Opt<Ref<Parent>>, Opt<Ref<Children>>>> windows,
                                ResMut<SFMLwindows> sfml_windows,
                                EventWriter<WindowCreated> window_created) {
    for (auto&& [id, desc, parent, children] : windows.iter()) {
        if (sfml_windows->contains(id)) continue;
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
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) continue;
        auto& desc   = mdesc.get_mut();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        auto* window = it->second.get();

        if (cached.title != desc.title) {
            window->setTitle(sf::String::fromUtf8(desc.title.begin(), desc.title.end()));
            cached.title = desc.title;
        }
        if (cached.visible != desc.visible) {
            window->setVisible(desc.visible);
            cached.visible = desc.visible;
        }
        if (cached.focused != desc.focused && desc.focused) {
            window->requestFocus();
        }
        desc.focused   = window->hasFocus();
        cached.focused = desc.focused;

        if (cached.cursor_mode != desc.cursor_mode) {
            window->setMouseCursorVisible(desc.cursor_mode == CursorMode::Normal);
            window->setMouseCursorGrabbed(desc.cursor_mode == CursorMode::Captured);
            cached.cursor_mode = desc.cursor_mode;
        }

        if (cached.icon != desc.icon) {
            if (desc.icon.has_value()) {
                images->get(desc.icon.value()).transform([&](const image::Image& img) {
                    auto expected_format = image::Format::RGBA8;
                    std::optional<image::Image> converted_img;
                    if (img.format() != expected_format) converted_img = img.convert(expected_format);
                    auto& image = converted_img.has_value() ? *converted_img : img;
                    auto view   = image.raw_view();
                    window->setIcon(image.width(), image.height(),
                                    reinterpret_cast<const std::uint8_t*>(view.data()));
                    return std::cref(img);
                });
            }
            cached.icon = desc.icon;
        }

        const auto size  = window->getSize();
        const auto mpos  = sf::Mouse::getPosition(*window);
        desc.cursor_pos  = {static_cast<double>(mpos.x), static_cast<double>(mpos.y)};
        cached.cursor_pos = desc.cursor_pos;
        desc.cursor_in_window = desc.focused && mpos.x >= 0 && mpos.y >= 0 && mpos.x < static_cast<int>(size.x) &&
                                mpos.y < static_cast<int>(size.y);
        cached.cursor_in_window = desc.cursor_in_window;
        desc.frame_size         = {};
        cached.frame_size       = desc.frame_size;

        cached.resizable            = desc.resizable;
        cached.decorations          = desc.decorations;
        cached.opacity              = desc.opacity;
        cached.size_limits          = desc.size_limits;
        cached.cursor_icon          = desc.cursor_icon;
        cached.composite_alpha_mode = desc.composite_alpha_mode;
        cached.present_mode         = desc.present_mode;
        cached.window_level         = desc.window_level;
        cached.maximized            = desc.maximized;
        cached.iconified            = desc.iconified;
    }
}

void SFMLPlugin::toggle_window_mode(Query<Item<Entity, Mut<Window>, const CachedWindow&>> windows,
                                    ResMut<SFMLwindows> sfml_windows) {
    for (auto&& [id, mdesc, cached_window] : windows.iter()) {
        if (auto it = sfml_windows->find(id); it == sfml_windows->end()) continue;
        auto& desc   = mdesc.get_mut();
        auto& cached = *const_cast<Window*>(reinterpret_cast<const Window*>(&cached_window));
        if (desc.window_mode == cached.window_mode) continue;

        auto recreated = create_window(id, desc);
        it->second->close();
        it->second       = std::move(recreated);
        cached.window_mode = desc.window_mode;
    }
}

void SFMLPlugin::poll_events() {}

void SFMLPlugin::send_cached_events(Query<Item<const CachedWindow&>> cached_windows,
                                    ResMut<SFMLwindows> sfml_windows,
                                    EventWriter<WindowResized> window_resized,
                                    EventWriter<WindowCloseRequested> window_close_requested,
                                    EventWriter<CursorMoved> cursor_moved,
                                    EventWriter<CursorEntered> cursor_entered,
                                    EventWriter<FileDrop> file_drop,
                                    EventWriter<ReceivedCharacter> received_character,
                                    EventWriter<WindowFocused> window_focused,
                                    EventWriter<WindowMoved> window_moved,
                                    std::optional<EventWriter<input::KeyInput>> key_input_event,
                                    std::optional<EventWriter<input::MouseButtonInput>> mouse_button_input,
                                    std::optional<EventWriter<input::MouseMove>> mouse_move_input,
                                    std::optional<EventWriter<input::MouseScroll>> scroll_input) {
    for (auto&& [id, window] : *sfml_windows) {
        sf::Event event{};
        while (window->pollEvent(event)) {
            switch (event.type) {
                case sf::Event::Closed:
                    window_close_requested.write(WindowCloseRequested{id});
                    break;
                case sf::Event::Resized:
                    window_resized.write(WindowResized{id, static_cast<int>(event.size.width),
                                                      static_cast<int>(event.size.height)});
                    break;
                case sf::Event::GainedFocus:
                    window_focused.write(WindowFocused{id, true});
                    break;
                case sf::Event::LostFocus:
                    window_focused.write(WindowFocused{id, false});
                    break;
                case sf::Event::TextEntered:
                    received_character.write(ReceivedCharacter{id, event.text.unicode});
                    break;
                case sf::Event::MouseEntered:
                    cursor_entered.write(CursorEntered{id, true});
                    break;
                case sf::Event::MouseLeft:
                    cursor_entered.write(CursorEntered{id, false});
                    break;
                case sf::Event::MouseMoved: {
                    auto old_pos = std::pair<double, double>{0.0, 0.0};
                    if (auto cached = cached_windows.get(id); cached.has_value()) {
                        old_pos = reinterpret_cast<const Window*>(&std::get<0>(*cached))->cursor_pos;
                    }
                    const auto new_pos = std::pair<double, double>{static_cast<double>(event.mouseMove.x),
                                                                    static_cast<double>(event.mouseMove.y)};
                    cursor_moved.write(CursorMoved{id, new_pos, {new_pos.first - old_pos.first, new_pos.second - old_pos.second}});
                    if (mouse_move_input) {
                        mouse_move_input->write(input::MouseMove{{new_pos.first - old_pos.first, new_pos.second - old_pos.second}});
                    }
                    break;
                }
                case sf::Event::MouseButtonPressed:
                case sf::Event::MouseButtonReleased:
                    if (mouse_button_input) {
                        mouse_button_input->write(input::MouseButtonInput{
                            map_sfml_mouse_button_to_input(event.mouseButton.button),
                            event.type == sf::Event::MouseButtonPressed,
                            id});
                    }
                    break;
                case sf::Event::MouseWheelScrolled:
                    if (scroll_input) {
                        if (event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel) {
                            scroll_input->write(input::MouseScroll{0.0, event.mouseWheelScroll.delta, id});
                        } else {
                            scroll_input->write(input::MouseScroll{event.mouseWheelScroll.delta, 0.0, id});
                        }
                    }
                    break;
                case sf::Event::KeyPressed:
                case sf::Event::KeyReleased:
                    if (key_input_event) {
                        key_input_event->write(
                            input::KeyInput{map_sfml_key_to_input(event.key.code), 0, event.type == sf::Event::KeyPressed,
                                            false, id});
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void SFMLPlugin::destroy_windows(Query<Item<Entity, const Window&>> windows,
                                 ResMut<SFMLwindows> sfml_windows,
                                 EventWriter<WindowClosed> window_closed,
                                 EventWriter<WindowDestroyed> window_destroyed) {
    std::unordered_set<Entity> still_alive;
    for (auto&& [id, window_desc] : windows.iter()) still_alive.insert(id);

    std::vector<Entity> to_erase;
    for (auto&& [id, sfml_window] : std::views::all(*sfml_windows) | std::views::filter([&](auto&& tuple) {
                                         auto&& [wid, _] = tuple;
                                         return !still_alive.contains(wid);
                                     })) {
        window_closed.write(WindowClosed{id});
        sfml_window->close();
        to_erase.emplace_back(id);
        window_destroyed.write(WindowDestroyed{id});
    }
    for (auto&& id : to_erase) sfml_windows->erase(id);
}

SFMLRunner::SFMLRunner(App& app) {
    check_exit    = make_system_unique([](EventReader<core::AppExit> exit_event) -> std::optional<int> {
        for (auto event : exit_event.read()) return event.code;
        return std::nullopt;
    });
    remove_window = make_system_unique([](Commands commands, Query<Item<Entity, Mut<window::Window>>> windows) {
        for (auto&& [id, window] : windows.iter()) commands.entity(id).despawn();
    });
    exit_access   = check_exit->initialize(app.world_mut());
    remove_access = remove_window->initialize(app.world_mut());

    create_windows_system = make_system_unique(SFMLPlugin::create_windows);
    create_windows_system->set_name("sfml_create_windows");
    update_size_system = make_system_unique(SFMLPlugin::update_size);
    update_size_system->set_name("sfml_update_size");
    update_pos_system = make_system_unique(SFMLPlugin::update_pos);
    update_pos_system->set_name("sfml_update_pos");
    toggle_window_mode_system = make_system_unique(SFMLPlugin::toggle_window_mode);
    toggle_window_mode_system->set_name("sfml_toggle_window_mode");
    update_window_states_system = make_system_unique(SFMLPlugin::update_window_states);
    update_window_states_system->set_name("sfml_update_window_states");
    destroy_windows_system = make_system_unique(SFMLPlugin::destroy_windows);
    destroy_windows_system->set_name("sfml_destroy_windows");
    send_cached_events_system = make_system_unique(SFMLPlugin::send_cached_events);
    send_cached_events_system->set_name("sfml_send_cached_events");
    clipboard_set_text_system = make_system_unique(Clipboard::set_text);
    clipboard_set_text_system->set_name("sfml_clipboard_set_text");
    clipboard_update_system = make_system_unique(Clipboard::update);
    clipboard_update_system->set_name("sfml_clipboard_update");

    auto sfml_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    app.world_scope(
        [&](World& world) { std::ranges::for_each(sfml_systems, [&](auto& sys) { sys->initialize(world); }); });
}

bool SFMLRunner::step(App& app) {
    auto sfml_systems = std::array{
        toggle_window_mode_system.get(), update_size_system.get(),        update_pos_system.get(),
        create_windows_system.get(),     send_cached_events_system.get(), update_window_states_system.get(),
        destroy_windows_system.get(),    clipboard_set_text_system.get(), clipboard_update_system.get(),
    };
    SFMLPlugin::poll_events();
    app.world_scope([&](World& world) {
        for (auto&& sys : sfml_systems) {
            sys->run({}, world)
                .transform([&]() { sys->apply_deferred(world); })
                .transform_error([&](const RunSystemError& error) {
                    std::visit(
                        visitor{[&](const ValidateParamError& validate_error) {
                                    spdlog::error("SFML System [{}] parameter validation error: type: {}, msg: {}",
                                                  sys->name(), validate_error.param_type.short_name(),
                                                  validate_error.message);
                                },
                                [&](const SystemException& sys_exception) {
                                    try {
                                        if (sys_exception.exception) std::rethrow_exception(sys_exception.exception);
                                    } catch (const std::exception& e) {
                                        spdlog::error("SFML System [{}] exception during run: {}", sys->name(),
                                                      e.what());
                                    } catch (...) {
                                        spdlog::error("SFML System [{}] unknown exception during run.", sys->name());
                                    }
                                }},
                        error);
                    return error;
                });
        }
        for (auto&& sys : extra_systems) {
            if (!sys->initialized()) sys->initialize(world);
            sys->run({}, world)
                .transform([&]() { sys->apply_deferred(world); })
                .transform_error([&](const RunSystemError& error) {
                    std::visit(visitor{[&](const ValidateParamError& validate_error) {
                                           spdlog::error(
                                               "SFML extra system [{}] parameter validation error: type: {}, msg: {}",
                                               sys->name(), validate_error.param_type.short_name(),
                                               validate_error.message);
                                       },
                                       [&](const SystemException& sys_exception) {
                                           try {
                                               if (sys_exception.exception)
                                                   std::rethrow_exception(sys_exception.exception);
                                           } catch (const std::exception& e) {
                                               spdlog::error("SFML extra system [{}] exception during run: {}",
                                                             sys->name(), e.what());
                                           } catch (...) {
                                               spdlog::error("SFML extra system [{}] unknown exception during run.",
                                                             sys->name());
                                           }
                                       }},
                               error);
                    return error;
                });
        }
    });
    app.update().wait();
    auto exit_code = app.system_dispatcher()->dispatch_system(*check_exit, {}, exit_access).get().value_or(-1);
    if (render_app_future) render_app_future->wait();
    render_app_future.reset();
    if (exit_code.has_value()) return false;
    render_app_future = render_app_label.and_then([&](const core::AppLabel& label) -> std::optional<std::future<bool>> {
        if (auto render_app = app.get_sub_app_mut(label)) {
            render_app.value().get().extract(app);
            return render_app.value().get().update();
        }
        return std::nullopt;
    });
    return !exit_code.has_value();
}

void SFMLRunner::exit(App& app) {
    if (render_app_future) render_app_future->wait();
    render_app_future.reset();
    app.world_scope([&](World& world) {
        auto res = remove_window->run({}, world);
        res      = destroy_windows_system->run({}, world);
    });
    app.run_schedules(PreExit, Exit, PostExit).wait();
}

void SFMLPlugin::build(App& app) {
    app.add_plugins(image::ImagePlugin{});
    app.world_mut().insert_resource(Clipboard{});
    app.world_mut().init_resource<SFMLwindows>();
    app.add_events<SetClipboardString>().set_runner(std::make_unique<SFMLRunner>(app));
}

input::MouseButton sfml::map_sfml_mouse_button_to_input(sf::Mouse::Button button) {
    switch (button) {
        case sf::Mouse::Button::Left:
            return input::MouseButton::MouseButtonLeft;
        case sf::Mouse::Button::Right:
            return input::MouseButton::MouseButtonRight;
        case sf::Mouse::Button::Middle:
            return input::MouseButton::MouseButtonMiddle;
        case sf::Mouse::Button::XButton1:
            return input::MouseButton::MouseButton4;
        case sf::Mouse::Button::XButton2:
            return input::MouseButton::MouseButton5;
        default:
            return input::MouseButton::MouseButtonUnknown;
    }
}

input::KeyCode sfml::map_sfml_key_to_input(sf::Keyboard::Key key) {
    switch (key) {
        case sf::Keyboard::A:
            return input::KeyCode::KeyA;
        case sf::Keyboard::B:
            return input::KeyCode::KeyB;
        case sf::Keyboard::C:
            return input::KeyCode::KeyC;
        case sf::Keyboard::D:
            return input::KeyCode::KeyD;
        case sf::Keyboard::E:
            return input::KeyCode::KeyE;
        case sf::Keyboard::F:
            return input::KeyCode::KeyF;
        case sf::Keyboard::G:
            return input::KeyCode::KeyG;
        case sf::Keyboard::H:
            return input::KeyCode::KeyH;
        case sf::Keyboard::I:
            return input::KeyCode::KeyI;
        case sf::Keyboard::J:
            return input::KeyCode::KeyJ;
        case sf::Keyboard::K:
            return input::KeyCode::KeyK;
        case sf::Keyboard::L:
            return input::KeyCode::KeyL;
        case sf::Keyboard::M:
            return input::KeyCode::KeyM;
        case sf::Keyboard::N:
            return input::KeyCode::KeyN;
        case sf::Keyboard::O:
            return input::KeyCode::KeyO;
        case sf::Keyboard::P:
            return input::KeyCode::KeyP;
        case sf::Keyboard::Q:
            return input::KeyCode::KeyQ;
        case sf::Keyboard::R:
            return input::KeyCode::KeyR;
        case sf::Keyboard::S:
            return input::KeyCode::KeyS;
        case sf::Keyboard::T:
            return input::KeyCode::KeyT;
        case sf::Keyboard::U:
            return input::KeyCode::KeyU;
        case sf::Keyboard::V:
            return input::KeyCode::KeyV;
        case sf::Keyboard::W:
            return input::KeyCode::KeyW;
        case sf::Keyboard::X:
            return input::KeyCode::KeyX;
        case sf::Keyboard::Y:
            return input::KeyCode::KeyY;
        case sf::Keyboard::Z:
            return input::KeyCode::KeyZ;
        case sf::Keyboard::Num0:
            return input::KeyCode::Key0;
        case sf::Keyboard::Num1:
            return input::KeyCode::Key1;
        case sf::Keyboard::Num2:
            return input::KeyCode::Key2;
        case sf::Keyboard::Num3:
            return input::KeyCode::Key3;
        case sf::Keyboard::Num4:
            return input::KeyCode::Key4;
        case sf::Keyboard::Num5:
            return input::KeyCode::Key5;
        case sf::Keyboard::Num6:
            return input::KeyCode::Key6;
        case sf::Keyboard::Num7:
            return input::KeyCode::Key7;
        case sf::Keyboard::Num8:
            return input::KeyCode::Key8;
        case sf::Keyboard::Num9:
            return input::KeyCode::Key9;
        case sf::Keyboard::Escape:
            return input::KeyCode::KeyEscape;
        case sf::Keyboard::LControl:
            return input::KeyCode::KeyLeftControl;
        case sf::Keyboard::LShift:
            return input::KeyCode::KeyLeftShift;
        case sf::Keyboard::LAlt:
            return input::KeyCode::KeyLeftAlt;
        case sf::Keyboard::LSystem:
            return input::KeyCode::KeyLeftSuper;
        case sf::Keyboard::RControl:
            return input::KeyCode::KeyRightControl;
        case sf::Keyboard::RShift:
            return input::KeyCode::KeyRightShift;
        case sf::Keyboard::RAlt:
            return input::KeyCode::KeyRightAlt;
        case sf::Keyboard::RSystem:
            return input::KeyCode::KeyRightSuper;
        case sf::Keyboard::Menu:
            return input::KeyCode::KeyMenu;
        case sf::Keyboard::LBracket:
            return input::KeyCode::KeyLeftBracket;
        case sf::Keyboard::RBracket:
            return input::KeyCode::KeyRightBracket;
        case sf::Keyboard::Semicolon:
            return input::KeyCode::KeySemicolon;
        case sf::Keyboard::Comma:
            return input::KeyCode::KeyComma;
        case sf::Keyboard::Period:
            return input::KeyCode::KeyPeriod;
        case sf::Keyboard::Quote:
            return input::KeyCode::KeyApostrophe;
        case sf::Keyboard::Slash:
            return input::KeyCode::KeySlash;
        case sf::Keyboard::Backslash:
            return input::KeyCode::KeyBackslash;
        case sf::Keyboard::Tilde:
            return input::KeyCode::KeyGraveAccent;
        case sf::Keyboard::Equal:
            return input::KeyCode::KeyEqual;
        case sf::Keyboard::Hyphen:
            return input::KeyCode::KeyMinus;
        case sf::Keyboard::Space:
            return input::KeyCode::KeySpace;
        case sf::Keyboard::Enter:
            return input::KeyCode::KeyEnter;
        case sf::Keyboard::Backspace:
            return input::KeyCode::KeyBackspace;
        case sf::Keyboard::Tab:
            return input::KeyCode::KeyTab;
        case sf::Keyboard::PageUp:
            return input::KeyCode::KeyPageUp;
        case sf::Keyboard::PageDown:
            return input::KeyCode::KeyPageDown;
        case sf::Keyboard::End:
            return input::KeyCode::KeyEnd;
        case sf::Keyboard::Home:
            return input::KeyCode::KeyHome;
        case sf::Keyboard::Insert:
            return input::KeyCode::KeyInsert;
        case sf::Keyboard::Delete:
            return input::KeyCode::KeyDelete;
        case sf::Keyboard::Add:
            return input::KeyCode::KeyKpAdd;
        case sf::Keyboard::Subtract:
            return input::KeyCode::KeyKpSubtract;
        case sf::Keyboard::Multiply:
            return input::KeyCode::KeyKpMultiply;
        case sf::Keyboard::Divide:
            return input::KeyCode::KeyKpDivide;
        case sf::Keyboard::Left:
            return input::KeyCode::KeyLeft;
        case sf::Keyboard::Right:
            return input::KeyCode::KeyRight;
        case sf::Keyboard::Up:
            return input::KeyCode::KeyUp;
        case sf::Keyboard::Down:
            return input::KeyCode::KeyDown;
        case sf::Keyboard::Numpad0:
            return input::KeyCode::KeyKp0;
        case sf::Keyboard::Numpad1:
            return input::KeyCode::KeyKp1;
        case sf::Keyboard::Numpad2:
            return input::KeyCode::KeyKp2;
        case sf::Keyboard::Numpad3:
            return input::KeyCode::KeyKp3;
        case sf::Keyboard::Numpad4:
            return input::KeyCode::KeyKp4;
        case sf::Keyboard::Numpad5:
            return input::KeyCode::KeyKp5;
        case sf::Keyboard::Numpad6:
            return input::KeyCode::KeyKp6;
        case sf::Keyboard::Numpad7:
            return input::KeyCode::KeyKp7;
        case sf::Keyboard::Numpad8:
            return input::KeyCode::KeyKp8;
        case sf::Keyboard::Numpad9:
            return input::KeyCode::KeyKp9;
        case sf::Keyboard::F1:
            return input::KeyCode::KeyF1;
        case sf::Keyboard::F2:
            return input::KeyCode::KeyF2;
        case sf::Keyboard::F3:
            return input::KeyCode::KeyF3;
        case sf::Keyboard::F4:
            return input::KeyCode::KeyF4;
        case sf::Keyboard::F5:
            return input::KeyCode::KeyF5;
        case sf::Keyboard::F6:
            return input::KeyCode::KeyF6;
        case sf::Keyboard::F7:
            return input::KeyCode::KeyF7;
        case sf::Keyboard::F8:
            return input::KeyCode::KeyF8;
        case sf::Keyboard::F9:
            return input::KeyCode::KeyF9;
        case sf::Keyboard::F10:
            return input::KeyCode::KeyF10;
        case sf::Keyboard::F11:
            return input::KeyCode::KeyF11;
        case sf::Keyboard::F12:
            return input::KeyCode::KeyF12;
        case sf::Keyboard::Pause:
            return input::KeyCode::KeyPause;
        default:
            return input::KeyCode::KeyUnknown;
    }
}

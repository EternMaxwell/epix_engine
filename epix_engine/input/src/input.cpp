#include <spdlog/spdlog.h>

#include "epix/input.h"


using namespace epix;
using namespace epix::input;
using namespace epix::prelude;
using namespace epix::input::systems;

static auto logger = spdlog::default_logger()->clone("input");

EPIX_API void systems::create_input_for_window(
    Command command,
    Query<
        Get<Entity, const window::components::Window>,
        Without<ButtonInput<KeyCode>, ButtonInput<MouseButton>>> query
) {
    ZoneScopedN("input::create_input_for_window");
    for (auto [entity, window] : query.iter()) {
        spdlog::debug("create input for window {}.", window.m_title);
        command.entity(entity).insert(ButtonInput<KeyCode>(entity));
        command.entity(entity).insert(ButtonInput<MouseButton>(entity));
    }
}

EPIX_API void systems::update_input(
    Query<
        Get<Entity,
            ButtonInput<KeyCode>,
            ButtonInput<MouseButton>,
            const window::components::Window>> query,
    EventReader<events::KeyEvent> key_event_reader,
    EventWriter<events::KeyEvent> key_event_writer,
    EventReader<events::MouseButtonEvent> mouse_button_event_reader,
    EventWriter<events::MouseButtonEvent> mouse_button_event_writer
) {
    ZoneScopedN("input::update_input");
    for (auto [entity, key_input, mouse_input, window] : query.iter()) {
        if (window.get_handle() == nullptr) {
            continue;
        }
        update_key_button_input(key_input, window.get_handle());
        update_mouse_button_input(mouse_input, window.get_handle());
    }
    key_event_reader.clear();
    mouse_button_event_reader.clear();
    for (auto [entity, key_input, mouse_input, window] : query.iter()) {
        for (auto key : key_input.just_pressed_keys()) {
            key_event_writer.write(events::KeyEvent{
                key, ButtonState::Pressed, key_input.key_name(key), entity
            });
        }
        for (auto key : key_input.pressed_keys()) {
            if (key_input.just_pressed_keys().find(key) ==
                key_input.just_pressed_keys().end()) {
                key_event_writer.write(events::KeyEvent{
                    key, ButtonState::Repeat, key_input.key_name(key), entity
                });
            }
        }
        for (auto key : key_input.just_released_keys()) {
            key_event_writer.write(events::KeyEvent{
                key, ButtonState::Released, key_input.key_name(key), entity
            });
        }
        for (auto button : mouse_input.just_pressed_buttons()) {
            mouse_button_event_writer.write(
                events::MouseButtonEvent{button, ButtonState::Pressed, entity}
            );
        }
        for (auto button : mouse_input.pressed_buttons()) {
            if (mouse_input.just_pressed_buttons().find(button) ==
                mouse_input.just_pressed_buttons().end()) {
                mouse_button_event_writer.write(events::MouseButtonEvent{
                    button, ButtonState::Repeat, entity
                });
            }
        }
        for (auto button : mouse_input.just_released_buttons()) {
            mouse_button_event_writer.write(
                events::MouseButtonEvent{button, ButtonState::Released, entity}
            );
        }
    }
}

EPIX_API void systems::output_event(
    EventReader<epix::input::events::MouseScroll> scroll_events,
    Query<Get<ButtonInput<KeyCode>, ButtonInput<MouseButton>, const Window>>
        query,
    EventReader<epix::input::events::CursorMove> cursor_move_events
) {
    ZoneScopedN("input::output_event");
    for (auto event : scroll_events.read()) {
        logger->info("scroll : {}", event.yoffset);
    }
    for (auto [key_input, mouse_input, window] : query.iter()) {
        for (auto key : key_input.just_pressed_keys()) {
            auto *key_name = key_input.key_name(key);
            if (key_name == nullptr) {
                logger->info("key {} just pressed", static_cast<int>(key));
            } else {
                logger->info("key {} just pressed", key_name);
            }
        }
        for (auto key : key_input.just_released_keys()) {
            auto *key_name = key_input.key_name(key);
            if (key_name == nullptr) {
                logger->info("key {} just released", static_cast<int>(key));
            } else {
                logger->info("key {} just released", key_name);
            }
        }
        for (auto button : mouse_input.just_pressed_buttons()) {
            logger->info(
                "mouse button {} just pressed", static_cast<int>(button)
            );
        }
        for (auto button : mouse_input.just_released_buttons()) {
            logger->info(
                "mouse button {} just released", static_cast<int>(button)
            );
        }
        // if (window.get_cursor_move().has_value()) {
        //     auto [x, y] = window.get_cursor_move().value();
        //     spdlog::info("cursor move -from window : {}, {}", x, y);
        // }
    }
    for (auto event : cursor_move_events.read()) {
        logger->info("cursor move -from events : {}, {}", event.x, event.y);
    }
}

EPIX_API InputPlugin &InputPlugin::enable_output() {
    enable_output_event = true;
    return *this;
}

EPIX_API InputPlugin &InputPlugin::disable_output() {
    enable_output_event = false;
    return *this;
}

EPIX_API void InputPlugin::build(App &app) {
    app.add_event<events::KeyEvent>();
    app.add_event<events::MouseButtonEvent>();
    app.add_system(epix::Startup, create_input_for_window)
        ->add_system(
            epix::First, epix::into(create_input_for_window, update_input)
                             .after(window::systems::poll_events)
                             .worker("single")
        );
    if (enable_output_event) {
        app.add_system(epix::PreUpdate, output_event);
    }
}

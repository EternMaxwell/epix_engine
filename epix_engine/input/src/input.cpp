#include <spdlog/spdlog.h>

#include "epix/input.hpp"
#include "epix/input/enums.hpp"

namespace epix {
void input::log_inputs(EventReader<KeyInput> key_reader,
                       EventReader<MouseButtonInput> mouse_reader,
                       EventReader<MouseMove> mouse_move_reader,
                       EventReader<MouseScroll> mouse_scroll_reader) {
    for (auto&& [key, scancode, pressed, repeat, window] : key_reader.read()) {
        spdlog::info("Key: {}, Scancode: {}, Pressed: {}, Repeat: {}", key_name(key), scancode, pressed, repeat);
    }
    for (auto&& [button, pressed, window] : mouse_reader.read()) {
        spdlog::info("Mouse Button: {}, Pressed: {}", mouse_button_name(button), pressed);
    }
    for (auto&& [delta] : mouse_move_reader.read()) {
        spdlog::info("Mouse Move: {}, {}", delta.first, delta.second);
    }
    for (auto&& [xoffset, yoffset, window] : mouse_scroll_reader.read()) {
        spdlog::info("Mouse Scroll: {}, {}", xoffset, yoffset);
    }
}

void input::InputPlugin::build(App& app) {
    app.add_events<KeyInput>().add_events<MouseButtonInput>().add_events<MouseMove>().add_events<MouseScroll>();

    app.world_mut().init_resource<ButtonInput<KeyCode>>();
    app.world_mut().init_resource<ButtonInput<MouseButton>>();
    app.add_systems(PreUpdate, into(ButtonInput<KeyCode>::collect_events, ButtonInput<MouseButton>::collect_events)
                                   .set_name("collect input events"));
}
}  // namespace epix
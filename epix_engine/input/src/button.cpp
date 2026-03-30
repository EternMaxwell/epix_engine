module;

#include <spdlog/spdlog.h>

module epix.input;

import :button;

namespace epix::input {
void ButtonInput<KeyCode>::collect_events(ResMut<ButtonInput<KeyCode>> input, EventReader<KeyInput> reader) {
    input->m_just_pressed.clear();
    input->m_just_released.clear();
    input->clear_bypass();
    for (const auto& event : reader.read()) {
        if (event.pressed) {
            if (!input->m_pressed.contains(event.key)) {
                spdlog::trace("[input] Key pressed: {}.", static_cast<int>(event.key));
                input->m_just_pressed.insert(event.key);
                input->m_pressed.insert(event.key);
            }
        } else {
            if (input->m_pressed.contains(event.key)) {
                spdlog::trace("[input] Key released: {}.", static_cast<int>(event.key));
                input->m_just_released.insert(event.key);
                input->m_pressed.erase(event.key);
            }
        }
    }
}
void ButtonInput<MouseButton>::collect_events(ResMut<ButtonInput<MouseButton>> input,
                                              EventReader<MouseButtonInput> reader) {
    input->m_just_pressed.clear();
    input->m_just_released.clear();
    input->clear_bypass();
    for (const auto& event : reader.read()) {
        if (event.pressed) {
            if (!input->m_pressed.contains(event.button)) {
                spdlog::trace("[input] Mouse button pressed: {}.", static_cast<int>(event.button));
                input->m_just_pressed.insert(event.button);
                input->m_pressed.insert(event.button);
            }
        } else {
            if (input->m_pressed.contains(event.button)) {
                spdlog::trace("[input] Mouse button released: {}.", static_cast<int>(event.button));
                input->m_just_released.insert(event.button);
                input->m_pressed.erase(event.button);
            }
        }
    }
}
}  // namespace epix::input

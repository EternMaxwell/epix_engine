#include "epix/input/button.h"

using namespace epix::input;

EPIX_API void ButtonInput<KeyCode>::collect_events(
    ResMut<ButtonInput<KeyCode>> input, EventReader<events::KeyInput> reader
) {
    // clear the just pressed and just released
    input->m_just_pressed.clear();
    input->m_just_released.clear();
    // read the events
    for (auto&& [key, scancode, pressed, repeat, window] : reader.read()) {
        if (pressed) {
            if (input->m_pressed.find(key) == input->m_pressed.end()) {
                input->m_just_pressed.insert(key);
                input->m_pressed.insert(key);
            }
        } else {
            input->m_pressed.erase(key);
            input->m_just_released.insert(key);
        }
    }
}

EPIX_API void ButtonInput<MouseButton>::collect_events(
    ResMut<ButtonInput<MouseButton>> input,
    EventReader<events::MouseButtonInput> reader
) {
    // clear the just pressed and just released
    input->m_just_pressed.clear();
    input->m_just_released.clear();
    // read the events
    for (auto&& [button, pressed, window] : reader.read()) {
        if (pressed) {
            if (input->m_pressed.find(button) == input->m_pressed.end()) {
                input->m_just_pressed.insert(button);
                input->m_pressed.insert(button);
            }
        } else {
            input->m_pressed.erase(button);
            input->m_just_released.insert(button);
        }
    }
}
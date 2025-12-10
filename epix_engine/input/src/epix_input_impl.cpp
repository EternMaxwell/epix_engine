// epix_input implementation - imports the module
module epix_input;

namespace epix::input {

// ============================================================================
// ButtonInput<KeyCode> implementation
// ============================================================================

void ButtonInput<KeyCode>::collect_events(core::ResMut<ButtonInput<KeyCode>> input, 
                                          core::EventReader<events::KeyInput> reader) {
    input->m_just_pressed.clear();
    input->m_just_released.clear();
    for (const auto& event : reader.read()) {
        if (event.pressed) {
            if (!input->m_pressed.contains(event.key)) {
                input->m_just_pressed.insert(event.key);
                input->m_pressed.insert(event.key);
            }
        } else {
            if (input->m_pressed.contains(event.key)) {
                input->m_just_released.insert(event.key);
                input->m_pressed.erase(event.key);
            }
        }
    }
}

// ============================================================================
// ButtonInput<MouseButton> implementation
// ============================================================================

void ButtonInput<MouseButton>::collect_events(core::ResMut<ButtonInput<MouseButton>> input,
                                              core::EventReader<events::MouseButtonInput> reader) {
    input->m_just_pressed.clear();
    input->m_just_released.clear();
    for (const auto& event : reader.read()) {
        if (event.pressed) {
            if (!input->m_pressed.contains(event.button)) {
                input->m_just_pressed.insert(event.button);
                input->m_pressed.insert(event.button);
            }
        } else {
            if (input->m_pressed.contains(event.button)) {
                input->m_just_released.insert(event.button);
                input->m_pressed.erase(event.button);
            }
        }
    }
}

// ============================================================================
// Enum helper functions
// ============================================================================

std::string_view key_name(KeyCode key) {
    switch (key) {
        case KeyCode::KeyA: return "A";
        case KeyCode::KeyB: return "B";
        case KeyCode::KeyC: return "C";
        case KeyCode::KeyD: return "D";
        case KeyCode::KeyE: return "E";
        case KeyCode::KeyF: return "F";
        case KeyCode::KeyG: return "G";
        case KeyCode::KeyH: return "H";
        case KeyCode::KeyI: return "I";
        case KeyCode::KeyJ: return "J";
        case KeyCode::KeyK: return "K";
        case KeyCode::KeyL: return "L";
        case KeyCode::KeyM: return "M";
        case KeyCode::KeyN: return "N";
        case KeyCode::KeyO: return "O";
        case KeyCode::KeyP: return "P";
        case KeyCode::KeyQ: return "Q";
        case KeyCode::KeyR: return "R";
        case KeyCode::KeyS: return "S";
        case KeyCode::KeyT: return "T";
        case KeyCode::KeyU: return "U";
        case KeyCode::KeyV: return "V";
        case KeyCode::KeyW: return "W";
        case KeyCode::KeyX: return "X";
        case KeyCode::KeyY: return "Y";
        case KeyCode::KeyZ: return "Z";
        case KeyCode::Key0: return "0";
        case KeyCode::Key1: return "1";
        case KeyCode::Key2: return "2";
        case KeyCode::Key3: return "3";
        case KeyCode::Key4: return "4";
        case KeyCode::Key5: return "5";
        case KeyCode::Key6: return "6";
        case KeyCode::Key7: return "7";
        case KeyCode::Key8: return "8";
        case KeyCode::Key9: return "9";
        case KeyCode::KeySpace: return "Space";
        case KeyCode::KeyApostrophe: return "Apostrophe(')";
        case KeyCode::KeyComma: return "Comma(,)";
        case KeyCode::KeyMinus: return "Minus(-)";
        case KeyCode::KeyPeriod: return "Period(.)";
        case KeyCode::KeySlash: return "Slash(/)";
        case KeyCode::KeySemicolon: return "Semicolon(;)";
        case KeyCode::KeyEqual: return "Equal(=)";
        case KeyCode::KeyLeftBracket: return "LeftBracket([)";
        case KeyCode::KeyBackslash: return "Backslash(\\)";
        case KeyCode::KeyRightBracket: return "RightBracket(])";
        case KeyCode::KeyGraveAccent: return "GraveAccent(`)";
        case KeyCode::KeyWorld1: return "World1";
        case KeyCode::KeyWorld2: return "World2";
        case KeyCode::KeyEscape: return "Escape";
        case KeyCode::KeyEnter: return "Enter";
        case KeyCode::KeyTab: return "Tab";
        case KeyCode::KeyBackspace: return "Backspace";
        case KeyCode::KeyInsert: return "Insert";
        case KeyCode::KeyDelete: return "Delete";
        case KeyCode::KeyRight: return "Right";
        case KeyCode::KeyLeft: return "Left";
        case KeyCode::KeyDown: return "Down";
        case KeyCode::KeyUp: return "Up";
        case KeyCode::KeyPageUp: return "PageUp";
        case KeyCode::KeyPageDown: return "PageDown";
        case KeyCode::KeyHome: return "Home";
        case KeyCode::KeyEnd: return "End";
        case KeyCode::KeyCapsLock: return "CapsLock";
        case KeyCode::KeyScrollLock: return "ScrollLock";
        case KeyCode::KeyNumLock: return "NumLock";
        case KeyCode::KeyPrintScreen: return "PrintScreen";
        case KeyCode::KeyPause: return "Pause";
        case KeyCode::KeyF1: return "F1";
        case KeyCode::KeyF2: return "F2";
        case KeyCode::KeyF3: return "F3";
        case KeyCode::KeyF4: return "F4";
        case KeyCode::KeyF5: return "F5";
        case KeyCode::KeyF6: return "F6";
        case KeyCode::KeyF7: return "F7";
        case KeyCode::KeyF8: return "F8";
        case KeyCode::KeyF9: return "F9";
        case KeyCode::KeyF10: return "F10";
        case KeyCode::KeyF11: return "F11";
        case KeyCode::KeyF12: return "F12";
        case KeyCode::KeyF13: return "F13";
        case KeyCode::KeyF14: return "F14";
        case KeyCode::KeyF15: return "F15";
        case KeyCode::KeyF16: return "F16";
        case KeyCode::KeyF17: return "F17";
        case KeyCode::KeyF18: return "F18";
        case KeyCode::KeyF19: return "F19";
        case KeyCode::KeyF20: return "F20";
        case KeyCode::KeyF21: return "F21";
        case KeyCode::KeyF22: return "F22";
        case KeyCode::KeyF23: return "F23";
        case KeyCode::KeyF24: return "F24";
        case KeyCode::KeyF25: return "F25";
        case KeyCode::KeyKp0: return "Kp0";
        case KeyCode::KeyKp1: return "Kp1";
        case KeyCode::KeyKp2: return "Kp2";
        case KeyCode::KeyKp3: return "Kp3";
        case KeyCode::KeyKp4: return "Kp4";
        case KeyCode::KeyKp5: return "Kp5";
        case KeyCode::KeyKp6: return "Kp6";
        case KeyCode::KeyKp7: return "Kp7";
        case KeyCode::KeyKp8: return "Kp8";
        case KeyCode::KeyKp9: return "Kp9";
        case KeyCode::KeyKpDecimal: return "KpDecimal(.)";
        case KeyCode::KeyKpDivide: return "KpDivide(/)";
        case KeyCode::KeyKpMultiply: return "KpMultiply(*)";
        case KeyCode::KeyKpSubtract: return "KpSubtract(-)";
        case KeyCode::KeyKpAdd: return "KpAdd(+)";
        case KeyCode::KeyKpEnter: return "KpEnter";
        case KeyCode::KeyKpEqual: return "KpEqual(=)";
        case KeyCode::KeyLeftShift: return "LeftShift";
        case KeyCode::KeyLeftControl: return "LeftControl";
        case KeyCode::KeyLeftAlt: return "LeftAlt";
        case KeyCode::KeyLeftSuper: return "LeftSuper";
        case KeyCode::KeyRightShift: return "RightShift";
        case KeyCode::KeyRightControl: return "RightControl";
        case KeyCode::KeyRightAlt: return "RightAlt";
        case KeyCode::KeyRightSuper: return "RightSuper";
        case KeyCode::KeyMenu: return "Menu";
        default: return "Unknown";
    }
}

std::string_view mouse_button_name(MouseButton button) {
    switch (button) {
        case MouseButton::MouseButton1: return "MouseButton1(Left)";
        case MouseButton::MouseButton2: return "MouseButton2(Right)";
        case MouseButton::MouseButton3: return "MouseButton3(Middle)";
        case MouseButton::MouseButton4: return "MouseButton4";
        case MouseButton::MouseButton5: return "MouseButton5";
        case MouseButton::MouseButton6: return "MouseButton6";
        case MouseButton::MouseButton7: return "MouseButton7";
        case MouseButton::MouseButton8: return "MouseButton8";
        case MouseButton::MouseButtonLast: return "Last";
        default: return "Unknown";
    }
}

// ============================================================================
// InputPlugin implementation
// ============================================================================

void InputPlugin::build(core::App& app) {
    using namespace events;
    
    app.add_events<KeyInput>()
       .add_events<MouseButtonInput>()
       .add_events<MouseMove>()
       .add_events<MouseScroll>();

    app.world_mut().init_resource<ButtonInput<KeyCode>>();
    app.world_mut().init_resource<ButtonInput<MouseButton>>();
    
    app.add_systems(core::PreUpdate, 
                   core::into(ButtonInput<KeyCode>::collect_events, 
                             ButtonInput<MouseButton>::collect_events)
                   .set_name("collect input events"));
}

// ============================================================================
// Utility functions
// ============================================================================

void log_inputs(core::EventReader<events::KeyInput> key_reader,
                core::EventReader<events::MouseButtonInput> mouse_reader,
                core::EventReader<events::MouseMove> mouse_move_reader,
                core::EventReader<events::MouseScroll> mouse_scroll_reader) {
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

}  // namespace epix::input

#include "epix/input.h"

using namespace epix;
using namespace epix::input;
using namespace epix::prelude;
using namespace epix::input::components;

EPIX_API size_t KeyHash::operator()(const KeyCode& key) const {
    return std::hash<int>()(static_cast<int>(key));
}

EPIX_API ButtonInput<KeyCode>::ButtonInput(Entity window) : m_window(window) {}
EPIX_API bool ButtonInput<KeyCode>::just_pressed(KeyCode key) const {
    return m_just_pressed.find(key) != m_just_pressed.end();
}
EPIX_API bool ButtonInput<KeyCode>::just_released(KeyCode key) const {
    return m_just_released.find(key) != m_just_released.end();
}
EPIX_API bool ButtonInput<KeyCode>::pressed(KeyCode key) const {
    return m_pressed.find(key) != m_pressed.end();
}
EPIX_API const entt::dense_set<KeyCode, KeyHash>&
ButtonInput<KeyCode>::just_pressed_keys() const {
    return m_just_pressed;
}
EPIX_API const entt::dense_set<KeyCode, KeyHash>&
ButtonInput<KeyCode>::just_released_keys() const {
    return m_just_released;
}
EPIX_API const entt::dense_set<KeyCode, KeyHash>&
ButtonInput<KeyCode>::pressed_keys() const {
    return m_pressed;
}
EPIX_API bool ButtonInput<KeyCode>::all_pressed(const std::vector<KeyCode>& keys
) const {
    for (auto key : keys) {
        if (!pressed(key)) return false;
    }
    return true;
}
EPIX_API bool ButtonInput<KeyCode>::any_just_pressed(
    const std::vector<KeyCode>& keys
) const {
    for (auto key : keys) {
        if (just_pressed(key)) return true;
    }
    return false;
}
EPIX_API bool ButtonInput<KeyCode>::any_just_released(
    const std::vector<KeyCode>& keys
) const {
    for (auto key : keys) {
        if (just_released(key)) return true;
    }
    return false;
}
EPIX_API bool ButtonInput<KeyCode>::any_pressed(const std::vector<KeyCode>& keys
) const {
    for (auto key : keys) {
        if (pressed(key)) return true;
    }
    return false;
}
EPIX_API const char* ButtonInput<KeyCode>::key_name(KeyCode key) const {
    return glfwGetKeyName(static_cast<int>(key), 0);
}
EPIX_API Entity ButtonInput<KeyCode>::linked_window() const { return m_window; }

static std::vector<KeyCode> keyCodeAll = {
    KeyA,
    KeyB,
    KeyC,
    KeyD,
    KeyE,
    KeyF,
    KeyG,
    KeyH,
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyM,
    KeyN,
    KeyO,
    KeyP,
    KeyQ,
    KeyR,
    KeyS,
    KeyT,
    KeyU,
    KeyV,
    KeyW,
    KeyX,
    KeyY,
    KeyZ,
    Key0,
    Key1,
    Key2,
    Key3,
    Key4,
    Key5,
    Key6,
    Key7,
    Key8,
    Key9,
    KeySpace,
    KeyApostrophe,
    KeyComma,
    KeyMinus,
    KeyPeriod,
    KeySlash,
    KeySemicolon,
    KeyEqual,
    KeyLeftBracket,
    KeyBackslash,
    KeyRightBracket,
    KeyGraveAccent,
    KeyWorld1,
    KeyWorld2,
    KeyEscape,
    KeyEnter,
    KeyTab,
    KeyBackspace,
    KeyInsert,
    KeyDelete,
    KeyRight,
    KeyLeft,
    KeyDown,
    KeyUp,
    KeyPageUp,
    KeyPageDown,
    KeyHome,
    KeyEnd,
    KeyCapsLock,
    KeyScrollLock,
    KeyNumLock,
    KeyPrintScreen,
    KeyPause,
    KeyF1,
    KeyF2,
    KeyF3,
    KeyF4,
    KeyF5,
    KeyF6,
    KeyF7,
    KeyF8,
    KeyF9,
    KeyF10,
    KeyF11,
    KeyF12,
    KeyF13,
    KeyF14,
    KeyF15,
    KeyF16,
    KeyF17,
    KeyF18,
    KeyF19,
    KeyF20,
    KeyF21,
    KeyF22,
    KeyF23,
    KeyF24,
    KeyF25,
    KeyKp0,
    KeyKp1,
    KeyKp2,
    KeyKp3,
    KeyKp4,
    KeyKp5,
    KeyKp6,
    KeyKp7,
    KeyKp8,
    KeyKp9,
    KeyKpDecimal,
    KeyKpDivide,
    KeyKpMultiply,
    KeyKpSubtract,
    KeyKpAdd,
    KeyKpEnter,
    KeyKpEqual,
    KeyLeftShift,
    KeyLeftControl,
    KeyLeftAlt,
    KeyLeftSuper,
    KeyRightShift,
    KeyRightControl,
    KeyRightAlt,
    KeyRightSuper,
    KeyMenu,
    KeyLast
};

EPIX_API void components::update_key_button_input(
    ButtonInput<KeyCode>& key_input, GLFWwindow* window
) {
    key_input.m_just_pressed.clear();
    key_input.m_just_released.clear();
    for (auto key : key_input.m_pressed) {
        if (glfwGetKey(window, static_cast<int>(key)) == GLFW_RELEASE) {
            key_input.m_just_released.insert(key);
        }
    }
    for (auto key : key_input.m_just_released) {
        key_input.m_pressed.erase(key);
    }
    for (auto key : keyCodeAll) {
        if (glfwGetKey(window, static_cast<int>(key)) == GLFW_PRESS) {
            if (key_input.m_pressed.find(key) == key_input.m_pressed.end())
                key_input.m_just_pressed.insert(key);
        }
    }
    key_input.m_pressed.insert(
        key_input.m_just_pressed.begin(), key_input.m_just_pressed.end()
    );
}

EPIX_API ButtonInput<MouseButton>::ButtonInput(Entity window)
    : m_window(window) {}
EPIX_API bool ButtonInput<MouseButton>::just_pressed(MouseButton button) const {
    return m_just_pressed.find(button) != m_just_pressed.end();
}
EPIX_API bool ButtonInput<MouseButton>::just_released(MouseButton button
) const {
    return m_just_released.find(button) != m_just_released.end();
}
EPIX_API bool ButtonInput<MouseButton>::pressed(MouseButton button) const {
    return m_pressed.find(button) != m_pressed.end();
}
EPIX_API const entt::dense_set<MouseButton, MouseButtonHash>&
ButtonInput<MouseButton>::just_pressed_buttons() const {
    return m_just_pressed;
}
EPIX_API const entt::dense_set<MouseButton, MouseButtonHash>&
ButtonInput<MouseButton>::just_released_buttons() const {
    return m_just_released;
}
EPIX_API const entt::dense_set<MouseButton, MouseButtonHash>&
ButtonInput<MouseButton>::pressed_buttons() const {
    return m_pressed;
}
EPIX_API bool ButtonInput<MouseButton>::any_just_pressed(
    const std::vector<MouseButton>& buttons
) const {
    for (auto button : buttons) {
        if (just_pressed(button)) return true;
    }
    return false;
}
EPIX_API bool ButtonInput<MouseButton>::any_just_released(
    const std::vector<MouseButton>& buttons
) const {
    for (auto button : buttons) {
        if (just_released(button)) return true;
    }
    return false;
}
EPIX_API bool ButtonInput<MouseButton>::any_pressed(
    const std::vector<MouseButton>& buttons
) const {
    for (auto button : buttons) {
        if (pressed(button)) return true;
    }
    return false;
}
EPIX_API bool ButtonInput<MouseButton>::all_pressed(
    const std::vector<MouseButton>& buttons
) const {
    for (auto button : buttons) {
        if (!pressed(button)) return false;
    }
    return true;
}
EPIX_API Entity ButtonInput<MouseButton>::linked_window() const {
    return m_window;
}

static std::vector<MouseButton> mouseButtonAll = {
    MouseButton1,    MouseButton2,    MouseButton3,     MouseButton4,
    MouseButton5,    MouseButton6,    MouseButton7,     MouseButton8,
    MouseButtonLast, MouseButtonLeft, MouseButtonRight, MouseButtonMiddle
};

EPIX_API void components::update_mouse_button_input(
    ButtonInput<MouseButton>& mouse_input, GLFWwindow* window
) {
    mouse_input.m_just_pressed.clear();
    mouse_input.m_just_released.clear();
    for (auto button : mouse_input.m_pressed) {
        if (glfwGetMouseButton(window, static_cast<int>(button)) ==
            GLFW_RELEASE) {
            mouse_input.m_just_released.insert(button);
        }
    }
    for (auto button : mouse_input.m_just_released) {
        mouse_input.m_pressed.erase(button);
    }
    for (auto button : mouseButtonAll) {
        if (glfwGetMouseButton(window, static_cast<int>(button)) ==
            GLFW_PRESS) {
            if (mouse_input.m_pressed.find(button) ==
                mouse_input.m_pressed.end())
                mouse_input.m_just_pressed.insert(button);
        }
    }
    mouse_input.m_pressed.insert(
        mouse_input.m_just_pressed.begin(), mouse_input.m_just_pressed.end()
    );
}
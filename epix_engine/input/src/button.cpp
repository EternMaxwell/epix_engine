#include "epix/input.h"

using namespace epix;
using namespace epix::input;

EPIX_API ButtonInput<KeyCode>::ButtonInput() {}
EPIX_API bool ButtonInput<KeyCode>::just_pressed(KeyCode key) const {
    return m_just_pressed.find(key) != m_just_pressed.end();
}
EPIX_API bool ButtonInput<KeyCode>::just_released(KeyCode key) const {
    return m_just_released.find(key) != m_just_released.end();
}
EPIX_API bool ButtonInput<KeyCode>::pressed(KeyCode key) const {
    return m_pressed.find(key) != m_pressed.end();
}
EPIX_API const entt::dense_set<KeyCode>&
ButtonInput<KeyCode>::just_pressed_keys() const {
    return m_just_pressed;
}
EPIX_API const entt::dense_set<KeyCode>&
ButtonInput<KeyCode>::just_released_keys() const {
    return m_just_released;
}
EPIX_API const entt::dense_set<KeyCode>& ButtonInput<KeyCode>::pressed_keys(
) const {
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

EPIX_API ButtonInput<MouseButton>::ButtonInput() {}
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
EPIX_API const entt::dense_set<MouseButton>&
ButtonInput<MouseButton>::just_pressed_buttons() const {
    return m_just_pressed;
}
EPIX_API const entt::dense_set<MouseButton>&
ButtonInput<MouseButton>::just_released_buttons() const {
    return m_just_released;
}
EPIX_API const entt::dense_set<MouseButton>&
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

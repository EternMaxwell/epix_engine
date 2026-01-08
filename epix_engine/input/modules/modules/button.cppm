export module epix.input:button;

import std;
import epix.core;

import :enums;
import :events;

using namespace core;

export namespace input {
template <typename T>
struct ButtonInput {};

template <>
struct ButtonInput<KeyCode> {
   public:
    ButtonInput() = default;

    bool just_pressed(KeyCode key) const { return m_just_pressed.contains(key); }
    bool just_released(KeyCode key) const { return m_just_released.contains(key); }
    bool pressed(KeyCode key) const { return m_pressed.contains(key); }

    auto just_pressed_keys() const { return std::views::all(m_just_pressed); }
    auto just_released_keys() const { return std::views::all(m_just_released); }
    auto pressed_keys() const { return std::views::all(m_pressed); }

    bool any_just_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return just_pressed(key); });
    }
    bool any_just_released(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return just_released(key); });
    }
    bool any_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return pressed(key); });
    }

    bool all_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::all_of(keys, [&](KeyCode key) { return pressed(key); });
    }

    static void collect_events(ResMut<ButtonInput<KeyCode>> input, EventReader<KeyInput> reader);

   private:
    std::unordered_set<KeyCode> m_just_pressed;
    std::unordered_set<KeyCode> m_just_released;
    std::unordered_set<KeyCode> m_pressed;
};

template <>
struct ButtonInput<MouseButton> {
   public:
    ButtonInput() = default;

    bool just_pressed(MouseButton button) const { return m_just_pressed.contains(button); }
    bool just_released(MouseButton button) const { return m_just_released.contains(button); }
    bool pressed(MouseButton button) const { return m_pressed.contains(button); }

    auto just_pressed_buttons() const { return std::views::all(m_just_pressed); }
    auto just_released_buttons() const { return std::views::all(m_just_released); }
    auto pressed_buttons() const { return std::views::all(m_pressed); }

    bool any_just_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return just_pressed(button); });
    }
    bool any_just_released(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return just_released(button); });
    }
    bool any_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return pressed(button); });
    }

    bool all_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::all_of(buttons, [&](MouseButton button) { return pressed(button); });
    }

    static void collect_events(ResMut<ButtonInput<MouseButton>> input, EventReader<MouseButtonInput> reader);

   private:
    std::unordered_set<MouseButton> m_just_pressed;
    std::unordered_set<MouseButton> m_just_released;
    std::unordered_set<MouseButton> m_pressed;
};
}  // namespace input
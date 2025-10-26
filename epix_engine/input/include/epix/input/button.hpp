#pragma once

#include <epix/core.hpp>
#include <unordered_set>

#include "enums.hpp"
#include "events.hpp"

namespace epix {
namespace input {
template <typename T>
struct ButtonInput {};

template <>
struct ButtonInput<KeyCode> {
   public:
    ButtonInput<KeyCode>();
    bool just_pressed(KeyCode key) const;
    bool just_released(KeyCode key) const;
    bool pressed(KeyCode key) const;
    const std::unordered_set<KeyCode>& just_pressed_keys() const;
    const std::unordered_set<KeyCode>& just_released_keys() const;
    const std::unordered_set<KeyCode>& pressed_keys() const;
    bool any_just_pressed(const std::vector<KeyCode>& keys) const;
    bool any_just_released(const std::vector<KeyCode>& keys) const;
    bool any_pressed(const std::vector<KeyCode>& keys) const;
    bool all_pressed(const std::vector<KeyCode>& keys) const;

    static void collect_events(ResMut<ButtonInput<KeyCode>> input, EventReader<events::KeyInput> reader);

   private:
    std::unordered_set<KeyCode> m_just_pressed;
    std::unordered_set<KeyCode> m_just_released;
    std::unordered_set<KeyCode> m_pressed;
};

template <>
struct ButtonInput<MouseButton> {
   public:
    ButtonInput<MouseButton>();

    bool just_pressed(MouseButton button) const;
    bool just_released(MouseButton button) const;
    bool pressed(MouseButton button) const;

    const std::unordered_set<MouseButton>& just_pressed_buttons() const;
    const std::unordered_set<MouseButton>& just_released_buttons() const;
    const std::unordered_set<MouseButton>& pressed_buttons() const;

    bool any_just_pressed(const std::vector<MouseButton>& buttons) const;
    bool any_just_released(const std::vector<MouseButton>& buttons) const;
    bool any_pressed(const std::vector<MouseButton>& buttons) const;

    bool all_pressed(const std::vector<MouseButton>& buttons) const;

    static void collect_events(ResMut<ButtonInput<MouseButton>> input,
                                EventReader<events::MouseButtonInput> reader);

   private:
    std::unordered_set<MouseButton> m_just_pressed;
    std::unordered_set<MouseButton> m_just_released;
    std::unordered_set<MouseButton> m_pressed;
};
}  // namespace input
}  // namespace epix
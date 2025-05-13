#pragma once

#include <epix/app.h>

#include "enums.h"
#include "events.h"

namespace epix {
namespace input {
template <typename T>
struct ButtonInput {};

template <>
struct ButtonInput<KeyCode> {
   public:
    EPIX_API ButtonInput<KeyCode>();

    EPIX_API bool just_pressed(KeyCode key) const;
    EPIX_API bool just_released(KeyCode key) const;
    EPIX_API bool pressed(KeyCode key) const;

    EPIX_API const entt::dense_set<KeyCode>& just_pressed_keys() const;
    EPIX_API const entt::dense_set<KeyCode>& just_released_keys() const;
    EPIX_API const entt::dense_set<KeyCode>& pressed_keys() const;

    EPIX_API bool any_just_pressed(const std::vector<KeyCode>& keys) const;
    EPIX_API bool any_just_released(const std::vector<KeyCode>& keys) const;
    EPIX_API bool any_pressed(const std::vector<KeyCode>& keys) const;

    EPIX_API bool all_pressed(const std::vector<KeyCode>& keys) const;

    EPIX_API static void collect_events(
        ResMut<ButtonInput<KeyCode>> input,
        EventReader<events::KeyInput> reader
    );

   private:
    entt::dense_set<KeyCode> m_just_pressed;
    entt::dense_set<KeyCode> m_just_released;
    entt::dense_set<KeyCode> m_pressed;
};

template <>
struct ButtonInput<MouseButton> {
   public:
    EPIX_API ButtonInput<MouseButton>();

    EPIX_API bool just_pressed(MouseButton button) const;
    EPIX_API bool just_released(MouseButton button) const;
    EPIX_API bool pressed(MouseButton button) const;

    EPIX_API const entt::dense_set<MouseButton>& just_pressed_buttons() const;
    EPIX_API const entt::dense_set<MouseButton>& just_released_buttons() const;
    EPIX_API const entt::dense_set<MouseButton>& pressed_buttons() const;

    EPIX_API bool any_just_pressed(const std::vector<MouseButton>& buttons
    ) const;
    EPIX_API bool any_just_released(const std::vector<MouseButton>& buttons
    ) const;
    EPIX_API bool any_pressed(const std::vector<MouseButton>& buttons) const;

    EPIX_API bool all_pressed(const std::vector<MouseButton>& buttons) const;

    EPIX_API static void collect_events(
        ResMut<ButtonInput<MouseButton>> input,
        EventReader<events::MouseButtonInput> reader
    );

   private:
    entt::dense_set<MouseButton> m_just_pressed;
    entt::dense_set<MouseButton> m_just_released;
    entt::dense_set<MouseButton> m_pressed;
};
}  // namespace input
}  // namespace epix
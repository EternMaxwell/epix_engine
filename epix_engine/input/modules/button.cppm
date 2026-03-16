export module epix.input:button;

import std;
import epix.core;

import :enums;
import :events;

using namespace core;

export namespace input {
/** @brief Tracks pressed/just-pressed/just-released state for a set of button keys.
 * @tparam T The button type (KeyCode or MouseButton).
 */
template <typename T>
struct ButtonInput {};

/** @brief Keyboard button input state tracker. */
template <>
struct ButtonInput<KeyCode> {
   public:
    ButtonInput() = default;

    /** @brief Check if the key was pressed this frame. */
    bool just_pressed(KeyCode key) const { return m_just_pressed.contains(key); }
    /** @brief Check if the key was released this frame. */
    bool just_released(KeyCode key) const { return m_just_released.contains(key); }
    /** @brief Check if the key is currently held down. */
    bool pressed(KeyCode key) const { return m_pressed.contains(key); }

    /** @brief Get all keys that were pressed this frame. */
    auto just_pressed_keys() const { return std::views::all(m_just_pressed); }
    /** @brief Get all keys that were released this frame. */
    auto just_released_keys() const { return std::views::all(m_just_released); }
    /** @brief Get all keys currently held down. */
    auto pressed_keys() const { return std::views::all(m_pressed); }

    /** @brief Check if any of the given keys were pressed this frame. */
    bool any_just_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return just_pressed(key); });
    }
    /** @brief Check if any of the given keys were released this frame. */
    bool any_just_released(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return just_released(key); });
    }
    /** @brief Check if any of the given keys are currently held down. */
    bool any_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::any_of(keys, [&](KeyCode key) { return pressed(key); });
    }

    /** @brief Check if all of the given keys are currently held down. */
    bool all_pressed(const std::vector<KeyCode>& keys) const {
        return std::ranges::all_of(keys, [&](KeyCode key) { return pressed(key); });
    }

    /** @brief System that reads KeyInput events and updates button state. */
    static void collect_events(ResMut<ButtonInput<KeyCode>> input, EventReader<KeyInput> reader);

   private:
    std::unordered_set<KeyCode> m_just_pressed;
    std::unordered_set<KeyCode> m_just_released;
    std::unordered_set<KeyCode> m_pressed;
};

/** @brief Mouse button input state tracker. */
template <>
struct ButtonInput<MouseButton> {
   public:
    ButtonInput() = default;

    /** @brief Check if the button was pressed this frame. */
    bool just_pressed(MouseButton button) const { return m_just_pressed.contains(button); }
    /** @brief Check if the button was released this frame. */
    bool just_released(MouseButton button) const { return m_just_released.contains(button); }
    /** @brief Check if the button is currently held down. */
    bool pressed(MouseButton button) const { return m_pressed.contains(button); }

    /** @brief Get all buttons that were pressed this frame. */
    auto just_pressed_buttons() const { return std::views::all(m_just_pressed); }
    /** @brief Get all buttons that were released this frame. */
    auto just_released_buttons() const { return std::views::all(m_just_released); }
    /** @brief Get all buttons currently held down. */
    auto pressed_buttons() const { return std::views::all(m_pressed); }

    /** @brief Check if any of the given buttons were pressed this frame. */
    bool any_just_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return just_pressed(button); });
    }
    /** @brief Check if any of the given buttons were released this frame. */
    bool any_just_released(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return just_released(button); });
    }
    /** @brief Check if any of the given buttons are currently held down. */
    bool any_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::any_of(buttons, [&](MouseButton button) { return pressed(button); });
    }

    /** @brief Check if all of the given buttons are currently held down. */
    bool all_pressed(const std::vector<MouseButton>& buttons) const {
        return std::ranges::all_of(buttons, [&](MouseButton button) { return pressed(button); });
    }

    /** @brief System that reads MouseButtonInput events and updates button
     * state. */
    static void collect_events(ResMut<ButtonInput<MouseButton>> input, EventReader<MouseButtonInput> reader);

   private:
    std::unordered_set<MouseButton> m_just_pressed;
    std::unordered_set<MouseButton> m_just_released;
    std::unordered_set<MouseButton> m_pressed;
};
}  // namespace input
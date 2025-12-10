/**
 * @file epix.input.cppm
 * @brief C++20 module interface for the input system.
 *
 * This module provides input handling for keyboard and mouse events.
 */
module;

#include <ranges>
#include <unordered_set>
#include <vector>

export module epix.input;

export import epix.core;

export namespace epix::input {

/**
 * @brief Key code enumeration for keyboard input.
 */
enum class KeyCode : int {
    Unknown = -1,
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,
    World1 = 161,
    World2 = 162,
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,
    Keypad0 = 320, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal = 330,
    KeypadDivide = 331,
    KeypadMultiply = 332,
    KeypadSubtract = 333,
    KeypadAdd = 334,
    KeypadEnter = 335,
    KeypadEqual = 336,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,
};

/**
 * @brief Mouse button enumeration.
 */
enum class MouseButton : int {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4,
    Button6 = 5,
    Button7 = 6,
    Button8 = 7,
};

/**
 * @brief Input state for button-like inputs.
 */
enum class InputState {
    Pressed,
    Released,
};

}  // namespace epix::input

export namespace epix::input::events {

/**
 * @brief Keyboard input event.
 */
struct KeyInput {
    KeyCode key;
    InputState state;
    int scancode;
    int mods;
};

/**
 * @brief Mouse button input event.
 */
struct MouseButtonInput {
    MouseButton button;
    InputState state;
    int mods;
};

/**
 * @brief Mouse movement event.
 */
struct MouseMove {
    double x;
    double y;
    double dx;
    double dy;
};

/**
 * @brief Mouse scroll event.
 */
struct MouseScroll {
    double x_offset;
    double y_offset;
};

}  // namespace epix::input::events

export namespace epix::input {

using namespace events;

/**
 * @brief Button input state tracker.
 * @tparam T The type of button (KeyCode or MouseButton).
 */
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

   private:
    std::unordered_set<MouseButton> m_just_pressed;
    std::unordered_set<MouseButton> m_just_released;
    std::unordered_set<MouseButton> m_pressed;
};

/**
 * @brief Plugin for the input system.
 */
struct InputPlugin {
    void build(epix::core::App& app);
};

}  // namespace epix::input

export namespace epix::input::prelude {
using input::ButtonInput;
using input::InputPlugin;
using input::KeyCode;
using input::MouseButton;
}  // namespace epix::input::prelude

export module epix.input:events;

import epix.core;
import std;

import :enums;

using namespace core;

export namespace input {
/** @brief Event fired on keyboard key press or release. */
struct KeyInput {
    /** @brief The key that was pressed or released. */
    KeyCode key;
    /** @brief Platform-specific scancode of the key. */
    int scancode;
    /** @brief True if the key was pressed, false if released. */
    bool pressed;
    /** @brief True if this is a key repeat event. */
    bool repeat;
    /** @brief Entity of the window that received the event. */
    Entity window;
};
/** @brief Event fired on mouse button press or release. */
struct MouseButtonInput {
    /** @brief The mouse button that was pressed or released. */
    MouseButton button;
    /** @brief True if the button was pressed, false if released. */
    bool pressed;
    /** @brief Entity of the window that received the event. */
    Entity window;
};
/** @brief Event fired when the mouse moves, containing the position delta. */
struct MouseMove {
    /** @brief The (dx, dy) movement delta since the last event. */
    std::pair<double, double> delta;
};
/** @brief Event fired on mouse scroll wheel input. */
struct MouseScroll {
    /** @brief Horizontal scroll offset. */
    double xoffset;
    /** @brief Vertical scroll offset. */
    double yoffset;
    /** @brief Entity of the window that received the event. */
    Entity window;
};
}  // namespace input
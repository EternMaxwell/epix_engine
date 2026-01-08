export module epix.input:events;

import epix.core;
import std;

import :enums;

using namespace core;

export namespace input {
struct KeyInput {
    KeyCode key;
    int scancode;
    bool pressed;
    bool repeat;
    Entity window;
};
struct MouseButtonInput {
    MouseButton button;
    bool pressed;
    Entity window;
};
struct MouseMove {
    std::pair<double, double> delta;
};
struct MouseScroll {
    double xoffset;
    double yoffset;
    Entity window;
};
}  // namespace input
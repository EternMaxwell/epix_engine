#pragma once

#include <epix/core.hpp>

#include "enums.hpp"

namespace epix::input::events {
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
}  // namespace epix::input::events
#pragma once

#include <epix/app.h>
#include <epix/window.h>

#include "components.h"

namespace epix {
namespace input {
namespace events {
using namespace epix;
using MouseScroll = window::events::MouseScroll;
using CursorMove  = window::events::CursorMove;
enum ButtonState { Pressed, Released, Repeat };
struct KeyEvent {
    components::KeyCode key;
    ButtonState state;
    const char* key_name;
    Entity window;
};
struct MouseButtonEvent {
    components::MouseButton button;
    ButtonState state;
    Entity window;
};
}  // namespace events
}  // namespace input
}  // namespace epix
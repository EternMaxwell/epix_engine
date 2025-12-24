#pragma once

#include <epix/core.hpp>

#include "input/button.hpp"
#include "input/enums.hpp"
#include "input/events.hpp"

namespace epix {
namespace input {
using namespace events;
struct InputPlugin {
    void build(App& app);
};
void log_inputs(EventReader<KeyInput> key_reader,
                EventReader<MouseButtonInput> mouse_reader,
                EventReader<MouseMove> mouse_move_reader,
                EventReader<MouseScroll> mouse_scroll_reader);
}  // namespace input
}  // namespace epix

namespace epix::input::prelude {
using input::ButtonInput;
using input::InputPlugin;
using input::KeyCode;
using input::MouseButton;
}  // namespace epix::input::prelude
namespace epix {
using namespace input::prelude;
}  // namespace epix
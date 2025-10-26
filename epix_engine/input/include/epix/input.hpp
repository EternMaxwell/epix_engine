#pragma once

#include <epix/core.hpp>

#include "input/button.hpp"
#include "input/enums.hpp"
#include "input/events.hpp"

namespace epix {
namespace input {
using namespace events;
struct InputPlugin {
    EPIX_API void build(App& app);
};
EPIX_API void print_inputs(
    EventReader<KeyInput> key_reader,
    EventReader<MouseButtonInput> mouse_reader,
    EventReader<MouseMove> mouse_move_reader,
    EventReader<MouseScroll> mouse_scroll_reader
);
}  // namespace input
}  // namespace epix
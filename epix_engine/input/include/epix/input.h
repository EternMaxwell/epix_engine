#pragma once

#include <epix/app.h>

#include "input/button.h"
#include "input/enums.h"
#include "input/events.h"

namespace epix {
namespace input {
using namespace events;
struct InputPlugin : Plugin {
    EPIX_API void build(App& app) override;
};
EPIX_API void print_inputs(
    EventReader<KeyInput> key_reader,
    EventReader<MouseButtonInput> mouse_reader,
    EventReader<MouseMove> mouse_move_reader,
    EventReader<MouseScroll> mouse_scroll_reader
);
}  // namespace input
}  // namespace epix

#pragma once

#include <epix/input/enums.hpp>
#include <epix/input/events.hpp>
#include <epix/input/button.hpp>

#include <epix/core.hpp>


namespace epix::input {
using namespace epix::core;
/** @brief Plugin that registers input event handling systems. */
struct InputPlugin {
    void attach(App& app);
};
/** @brief Debug system that logs all received input events to the console. */
void log_inputs(EventReader<KeyInput> key_reader,
                EventReader<MouseButtonInput> mouse_reader,
                EventReader<MouseMove> mouse_move_reader,
                EventReader<MouseScroll> mouse_scroll_reader);
}  // namespace input

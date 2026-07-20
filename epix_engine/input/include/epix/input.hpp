#pragma once

#include <epix/common.hpp>
#include <epix/input/button.hpp>
#include <epix/input/enums.hpp>
#include <epix/input/events.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/core.hpp>
#endif

EPIX_EXPORT namespace epix::input {
    /** @brief Plugin that registers input event handling systems. */
    struct InputPlugin {
        void attach(epix::core::App& app);
    };
    /** @brief Debug system that logs all received input events to the console. */
    void log_inputs(
        epix::core::EventReader<KeyInput> key_reader, epix::core::EventReader<MouseButtonInput> mouse_reader,
        epix::core::EventReader<MouseMove> mouse_move_reader, epix::core::EventReader<MouseScroll> mouse_scroll_reader);
}  // namespace epix::input

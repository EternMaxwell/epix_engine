#pragma once

#include <epix/common.hpp>
#include <epix/input/button.hpp>
#include <epix/input/enums.hpp>
#include <epix/input/events.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#endif

EPIX_EXPORT namespace epix::input {
    /** @brief Plugin that registers input event handling systems. */
    struct InputPlugin {
        void attach(epix::app::App& app);
    };
    /** @brief Debug system that logs all received input events to the console. */
    void log_inputs(epix::ecs::EventReader<KeyInput> key_reader, epix::ecs::EventReader<MouseButtonInput> mouse_reader,
                    epix::ecs::EventReader<MouseMove> mouse_move_reader,
                    epix::ecs::EventReader<MouseScroll> mouse_scroll_reader);
}  // namespace epix::input

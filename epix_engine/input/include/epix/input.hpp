#pragma once

#include <epix/input/button.hpp>
#include <epix/input/enums.hpp>
#include <epix/input/events.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/common.hpp>
#endif

EPIX_EXPORT namespace epix::input {
    /** @brief Plugin that registers input event handling systems. */
    struct EPIX_API InputPlugin {
        void attach(epix::app::App& app);
    };
    /** @brief Debug system that logs all received input events to the console. */
    EPIX_API void log_inputs(
        epix::ecs::EventReader<KeyInput> key_reader, epix::ecs::EventReader<MouseButtonInput> mouse_reader,
        epix::ecs::EventReader<MouseMove> mouse_move_reader, epix::ecs::EventReader<MouseScroll> mouse_scroll_reader);
}  // namespace epix::input

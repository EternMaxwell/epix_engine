#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <optional>
#endif

#include <epix/window/events.hpp>
#include <epix/window/structs.hpp>
#include <epix/window/system.hpp>

EPIX_EXPORT namespace epix::window {
    /** @brief Determines when the application should exit based on window
     * state. */
    enum class ExitCondition {
        /** @brief Exit when all windows are closed. */
        OnAllClosed,
        /** @brief Exit when the primary window is closed. */
        OnPrimaryClosed,
        /** @brief Never auto-exit due to window closure. */
        None,
    };
    /** @brief Plugin that creates the primary window and registers window
     * lifecycle systems.
     *
     * Set `primary_window` to std::nullopt to skip creating a default window.
     */
    struct WindowPlugin {
        /** @brief Configuration for the primary window. Nullopt skips
         * creation. */
        std::optional<Window> primary_window = Window{};
        /** @brief When the application should automatically exit. */
        ExitCondition exit_condition = ExitCondition::OnPrimaryClosed;
        /** @brief Whether to despawn window entities on close request. */
        bool close_when_requested = true;
        void attach(epix::app::App& app);
        void ready(epix::app::App& app);
    };

    /** @brief Debug system that logs all window events to the logger. */
    void log_events(
        epix::ecs::EventReader<WindowResized> resized, epix::ecs::EventReader<WindowMoved> moved,
        epix::ecs::EventReader<WindowCreated> created, epix::ecs::EventReader<WindowClosed> closed,
        epix::ecs::EventReader<WindowCloseRequested> close_requested, epix::ecs::EventReader<WindowDestroyed> destroyed,
        epix::ecs::EventReader<CursorMoved> cursor_moved, epix::ecs::EventReader<CursorEntered> cursor_entered,
        epix::ecs::EventReader<FileDrop> file_drop, epix::ecs::EventReader<ReceivedCharacter> received_character,
        epix::ecs::EventReader<WindowFocused> window_focused, epix::ecs::Query<epix::ecs::Item<const Window&>> windows);
}  // namespace epix::window
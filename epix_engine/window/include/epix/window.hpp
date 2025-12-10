#pragma once

#include <epix/api/macros.hpp>

#include "window/events.hpp"
#include "window/window.hpp"

EPIX_MODULE_EXPORT namespace epix::window {
enum class ExitCondition {
    OnAllClosed,
    OnPrimaryClosed,
    None,
};
struct WindowPlugin {
    std::optional<Window> primary_window = Window{};
    ExitCondition exit_condition         = ExitCondition::OnPrimaryClosed;
    bool close_when_requested            = true;
    void build(App& app);
    void finish(App& app);
};

void log_events(EventReader<events::WindowResized> resized,
                EventReader<events::WindowMoved> moved,
                EventReader<events::WindowCreated> created,
                EventReader<events::WindowClosed> closed,
                EventReader<events::WindowCloseRequested> close_requested,
                EventReader<events::WindowDestroyed> destroyed,
                EventReader<events::CursorMoved> cursor_moved,
                EventReader<events::CursorEntered> cursor_entered,
                EventReader<events::FileDrop> file_drop,
                EventReader<events::ReceivedCharacter> received_character,
                EventReader<events::WindowFocused> window_focused,
                Query<Item<const window::Window&>> windows);

using events::CursorEntered;
using events::CursorMoved;
using events::FileDrop;
using events::ReceivedCharacter;
using events::WindowClosed;
using events::WindowCloseRequested;
using events::WindowCreated;
using events::WindowDestroyed;
using events::WindowFocused;
using events::WindowMoved;
using events::WindowResized;
}  // namespace epix::window

namespace epix::render {
struct RenderT; // forward declare for render subapp.
}
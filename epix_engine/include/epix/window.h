#pragma once

#include "epix/app.h"
#include "epix/input.h"
#include "window/events.h"
#include "window/glfw.h"
#include "window/system.h"
#include "window/window.h"

namespace epix {
namespace window {
struct Focus {
    std::optional<Entity> focus = std::nullopt;

    EPIX_API std::optional<Entity>& operator*();
    EPIX_API const std::optional<Entity>& operator*() const;
};
enum class ExitCondition {
    OnAllClosed,
    OnPrimaryClosed,
    None,
};
struct WindowPlugin {
    std::optional<Window> primary_window = Window{};
    ExitCondition exit_condition         = ExitCondition::OnPrimaryClosed;
    bool close_when_requested            = true;
    EPIX_API void build(App& app);
};

EPIX_API void print_events(EventReader<events::WindowResized> resized,
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
                           Query<Item<window::Window>> windows);

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
}  // namespace window
}  // namespace epix
#pragma once

#include <epix/app.h>
#include <epix/window.h>

namespace epix {
namespace input {
namespace systems {
using namespace components;
using namespace events;
using namespace window::components;
using namespace window::events;

EPIX_API void create_input_for_window(
    Commands command,
    Query<
        Get<Entity, const Window>,
        Filter<Without<ButtonInput<KeyCode>, ButtonInput<MouseButton>>>> query
);
EPIX_API void update_input(
    Query<
        Get<Entity,
            ButtonInput<KeyCode>,
            ButtonInput<MouseButton>,
            const Window>> query,
    // EventReader<events::KeyEvent> key_event_reader,
    EventWriter<events::KeyEvent> key_event_writer,
    // EventReader<events::MouseButtonEvent> mouse_button_event_reader,
    EventWriter<events::MouseButtonEvent> mouse_button_event_writer
);

EPIX_API void output_event(
    EventReader<epix::input::events::MouseScroll> scroll_events,
    Query<Get<ButtonInput<KeyCode>, ButtonInput<MouseButton>, const Window>>
        query,
    EventReader<epix::input::events::CursorMove> cursor_move_events
);
}  // namespace systems
}  // namespace input
}  // namespace epix
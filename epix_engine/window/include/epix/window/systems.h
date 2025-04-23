#pragma once

#include <epix/app.h>

#include "components.h"
#include "events.h"
#include "resources.h"

namespace epix {
namespace window {

class WindowPlugin;
namespace systems {
using namespace components;
using namespace events;
using namespace epix::prelude;

EPIX_API void init_glfw();
EPIX_API void create_window_thread_pool(Command command);
EPIX_API void insert_primary_window(
    Command command, ResMut<window::WindowPlugin> window_plugin
);
EPIX_API void create_window(
    Command command,
    Query<Get<Entity, const WindowDescription>, Without<Window>> desc_query,
    ResMut<resources::WindowThreadPool> pool
);
EPIX_API void update_window_state(
    Query<Get<Entity, Window>> query,
    // EventReader<CursorMove> cursor_read,
    EventWriter<CursorMove> cursor_event
);
EPIX_API void close_window(
    Command command,
    EventReader<AnyWindowClose> any_close_event,
    Query<Get<Window>> query
);
EPIX_API void primary_window_close(
    Command command,
    Query<Get<Entity, Window>, With<PrimaryWindow>> query,
    EventWriter<AnyWindowClose> any_close_event
);
EPIX_API void window_close(
    Command command,
    Query<Get<Entity, Window>, Without<PrimaryWindow>> query,
    EventWriter<AnyWindowClose> any_close_event
);
EPIX_API void no_window_exists(
    Query<Get<Window>> query, EventWriter<NoWindowExists> no_window_event
);
EPIX_API void poll_events(
    ResMut<resources::WindowThreadPool> pool,
    Local<std::future<void>> future,
    Query<Get<Window>, With<PrimaryWindow>> query
);
EPIX_API void scroll_events(
    // EventReader<MouseScroll> scroll_read,
    EventWriter<MouseScroll> scroll_event
);
EPIX_API void exit_on_no_window(
    EventReader<NoWindowExists> no_window_event, EventWriter<AppExit> exit_event
);
}  // namespace systems
}  // namespace window
}  // namespace epix
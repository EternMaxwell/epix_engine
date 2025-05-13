#pragma once

#include "events.h"
#include "window.h"

namespace epix::window {
struct PrimaryWindow {};
EPIX_API void exit_on_all_closed(
    EventWriter<AppExit>& exit_writer, Query<Get<window::Window>>& query
);
EPIX_API void exit_on_primary_closed(
    EventWriter<AppExit>& exit_writer,
    Query<Get<window::Window>, With<PrimaryWindow>>& query
);
EPIX_API void close_requested(
    Commands& commands,
    Query<Get<Entity, window::Window>> windows,
    EventReader<events::WindowCloseRequested>& reader
);
}  // namespace epix::window

#pragma once

#include "events.h"
#include "window.h"

namespace epix::window {
struct PrimaryWindow {};
EPIX_API void exit_on_all_closed(
    EventWriter<AppExit>& exit_writer, Query<Get<window::Window>>& query
) {
    if (query.empty()) {
        exit_writer.write(AppExit{0});
    }
}
EPIX_API void exit_on_primary_closed(
    EventWriter<AppExit>& exit_writer,
    Query<Get<window::Window>, With<PrimaryWindow>>& query
) {
    if (query.empty()) {
        exit_writer.write(AppExit{0});
    }
}
EPIX_API void close_requested(
    Commands& commands,
    EventReader<events::WindowCloseRequested>& reader,
    EventWriter<events::WindowClosed>& closed_writer
) {
    for (auto&& [window] : reader.read()) {
        commands.entity(window).despawn();
        closed_writer.write(events::WindowClosed{window});
    }
}
}  // namespace epix::window.init_resource<GLFWExecutor>();

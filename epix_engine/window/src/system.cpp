#include "epix/window/system.h"

EPIX_API void epix::window::exit_on_all_closed(
    EventWriter<AppExit>& exit_writer, Query<Get<window::Window>>& query
) {
    if (query.empty()) {
        exit_writer.write(AppExit{0});
    }
}
EPIX_API void epix::window::exit_on_primary_closed(
    EventWriter<AppExit>& exit_writer,
    Query<Get<window::Window>, With<PrimaryWindow>>& query
) {
    if (query.empty()) {
        exit_writer.write(AppExit{0});
    }
}
EPIX_API void epix::window::close_requested(
    Commands& commands,
    Query<Get<Entity, window::Window>> windows,
    EventReader<events::WindowCloseRequested>& reader
) {
    for (auto&& [window] : reader.read()) {
        if (windows.contains(window)) {
            commands.entity(window).despawn();
        }
    }
}
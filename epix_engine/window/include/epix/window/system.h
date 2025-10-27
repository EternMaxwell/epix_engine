#pragma once

#include "events.h"
#include "window.h"

namespace epix::window {
EPIX_API void exit_on_all_closed(EventWriter<AppExit>& exit_writer,
                                 Local<entt::dense_set<Entity>> still_alive,
                                 EventReader<events::WindowCreated> created,
                                 EventReader<events::WindowDestroyed> destroyed);
EPIX_API void exit_on_primary_closed(EventWriter<AppExit>& exit_writer,
                                     Query<Item<Entity>, With<window::Window, PrimaryWindow>>& query,
                                     Local<std::optional<Entity>> primary_window,
                                     EventReader<events::WindowDestroyed> destroyed);
EPIX_API void close_requested(Commands& commands,
                              Query<Item<Entity, window::Window>> windows,
                              EventReader<events::WindowCloseRequested>& reader);
}  // namespace epix::window

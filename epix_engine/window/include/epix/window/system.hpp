#pragma once

#include "events.hpp"
#include "window.hpp"

namespace epix::window {
void exit_on_all_closed(EventWriter<AppExit> exit_writer,
                        Local<std::unordered_set<Entity>> still_alive,
                        EventReader<events::WindowCreated> created,
                        EventReader<events::WindowDestroyed> destroyed);
void exit_on_primary_closed(EventWriter<AppExit> exit_writer,
                            Query<Item<Entity>, With<window::Window, PrimaryWindow>> query,
                            Local<std::optional<Entity>> primary_window,
                            EventReader<events::WindowDestroyed> destroyed);
void close_requested(Commands commands,
                     Query<Item<Entity, const window::Window&>> windows,
                     EventReader<events::WindowCloseRequested> reader);
}  // namespace epix::window

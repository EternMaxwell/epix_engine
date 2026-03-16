export module epix.window:system;

import epix.core;

import :structs;
import :events;

namespace window {
void exit_on_all_closed(core::EventWriter<core::AppExit> exit_writer,
                        core::Local<std::unordered_set<core::Entity>> still_alive,
                        core::EventReader<WindowCreated> created,
                        core::EventReader<WindowDestroyed> destroyed);
void exit_on_primary_closed(core::EventWriter<core::AppExit> exit_writer,
                            core::Query<core::Item<core::Entity>, core::With<window::Window, PrimaryWindow>> query,
                            core::Local<std::optional<core::Entity>> primary_window,
                            core::EventReader<WindowDestroyed> destroyed);
void close_requested(core::Commands commands,
                     core::Query<core::Item<core::Entity, const window::Window&>> windows,
                     core::EventReader<WindowCloseRequested> reader);
}  // namespace window
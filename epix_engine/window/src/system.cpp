#include "epix/window/system.hpp"

using namespace epix;

void epix::window::exit_on_all_closed(EventWriter<AppExit> exit_writer,
                                      Local<std::unordered_set<Entity>> still_alive,
                                      EventReader<events::WindowCreated> created,
                                      EventReader<events::WindowDestroyed> destroyed) {
    for (auto&& [window] : created.read()) {
        still_alive->insert(window);
    }
    for (auto&& [window] : destroyed.read()) {
        still_alive->erase(window);
    }
    if (still_alive->empty()) {
        exit_writer.write(AppExit{0});
    }
}
void epix::window::exit_on_primary_closed(EventWriter<AppExit> exit_writer,
                                          Query<Item<Entity>, With<window::Window, PrimaryWindow>> query,
                                          Local<std::optional<Entity>> primary_window,
                                          EventReader<events::WindowDestroyed> destroyed) {
    // primary cache empty and no primary window set, exit
    if (query.empty() && !primary_window->has_value()) {
        exit_writer.write(AppExit{0});
        return;
    }
    // set primary cache if not set
    if (!primary_window->has_value()) {
        auto [window] = query.single().value();
        primary_window->emplace(window);
    }
    // if primary window is destroyed, exit
    for (auto&& [window] : destroyed.read()) {
        if (**primary_window == window) {
            exit_writer.write(AppExit{0});
            return;
        }
    }
}
void epix::window::close_requested(Commands commands,
                                   Query<Item<Entity, const window::Window&>> windows,
                                   EventReader<events::WindowCloseRequested> reader) {
    for (auto&& [window] : reader.read()) {
        if (windows.contains(window)) {
            commands.entity(window).despawn();
        }
    }
}
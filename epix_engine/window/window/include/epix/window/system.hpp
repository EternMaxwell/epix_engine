#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <optional>
#include <unordered_set>
#endif

#include <epix/window/events.hpp>
#include <epix/window/structs.hpp>

namespace epix::window {
EPIX_API void exit_on_all_closed(epix::ecs::EventWriter<epix::app::AppExit> exit_writer,
                                 epix::ecs::Local<std::unordered_set<epix::ecs::Entity>> still_alive,
                                 epix::ecs::EventReader<WindowCreated> created,
                                 epix::ecs::EventReader<WindowDestroyed> destroyed);
EPIX_API void exit_on_primary_closed(
    epix::ecs::EventWriter<epix::app::AppExit> exit_writer,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity>, epix::ecs::With<window::Window, PrimaryWindow>> query,
    epix::ecs::Local<std::optional<epix::ecs::Entity>> primary_window,
    epix::ecs::EventReader<WindowDestroyed> destroyed);
EPIX_API void close_requested(epix::ecs::Commands commands,
                              epix::ecs::Query<epix::ecs::Item<epix::ecs::Entity, const window::Window&>> windows,
                              epix::ecs::EventReader<WindowCloseRequested> reader);
}  // namespace epix::window
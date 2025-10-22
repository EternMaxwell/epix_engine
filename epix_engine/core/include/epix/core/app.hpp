#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "app/app_sche.hpp"
#include "app/schedules.hpp"
#include "world.hpp"

namespace epix::core {
EPIX_MAKE_LABEL(AppLabel)
struct WorldNotOwnedError {};
struct App {
   public:
    ~App() {
        _dispatcher.reset();
    }

    AppLabel label() const { return _label; }

    std::expected<std::reference_wrapper<const World>, WorldNotOwnedError> get_world() const {
        if (_world) {
            return *_world;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    const World& world() const { return get_world().value(); }
    std::expected<std::reference_wrapper<World>, WorldNotOwnedError> get_world_mut() {
        if (_world) {
            return *_world;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    World& world_mut() { return get_world_mut().value(); }

    App& add_schedule(schedule::Schedule&& schedule) {
        app::Schedules& schedules = world_mut().resource_or_init<app::Schedules>();
        // add or replace existing schedule
        schedules.add_schedule(std::move(schedule));
        return *this;
    }
    app::Schedules& schedules() { return world_mut().resource_or_init<app::Schedules>(); }
    std::optional<std::reference_wrapper<const app::Schedules>> get_schedules() const {
        auto world_opt = get_world();
        if (world_opt.has_value()) {
            auto& world = world_opt.value().get();
            return world.get_resource<app::Schedules>();
        }
        return std::nullopt;
    }
    App& add_systems(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
        schedules().schedule_or_insert((schedule::ScheduleLabel&)schedule).add_systems(std::move(config));
        return *this;
    }
    App& configure_sets(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
        schedules().schedule_or_insert((schedule::ScheduleLabel&)schedule).configure_sets(std::move(config));
        return *this;
    }

    std::optional<std::shared_ptr<schedule::SystemDispatcher>> system_dispatcher() {
        if (_dispatcher) {
            return _dispatcher;
        } else if (_world) {
            _dispatcher = std::shared_ptr<schedule::SystemDispatcher>(new schedule::SystemDispatcher(std::move(_world)),
                                                                      [this](schedule::SystemDispatcher* dispatcher) {
                                                                          // retrieve world back
                                                                          // since only the shared ptr have the ability
                                                                          // to modify the ptr, it is safe not having
                                                                          // extra synchronization here
                                                                          this->_world = dispatcher->release_world();
                                                                          delete dispatcher;
                                                                      });
            return _dispatcher;
        }
        return std::nullopt;
    }

   private:
    AppLabel _label = AppLabel::from_type<App>();

    std::unique_ptr<World> _world;
    std::shared_ptr<schedule::SystemDispatcher> _dispatcher;
};
}  // namespace epix::core
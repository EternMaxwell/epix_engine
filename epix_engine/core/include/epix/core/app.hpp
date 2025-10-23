#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "app/app_sche.hpp"
#include "app/schedules.hpp"
#include "query/access.hpp"
#include "schedule/schedule.hpp"
#include "system/system.hpp"
#include "world.hpp"

namespace epix::core {
EPIX_MAKE_LABEL(AppLabel)
struct WorldNotOwnedError {};
struct App {
   public:
    ~App() { _dispatcher.reset(); }

    AppLabel label() const { return _label; }

    std::expected<std::reference_wrapper<const World>, WorldNotOwnedError> get_world() const {
        std::lock_guard lock(_world_mutex);
        if (_world) {
            return *_world;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    const World& world() const { return get_world().value(); }
    std::expected<std::reference_wrapper<World>, WorldNotOwnedError> get_world_mut() {
        std::lock_guard lock(_world_mutex);
        if (_world) {
            return *_world;
        }
        return std::unexpected(WorldNotOwnedError{});
    }
    World& world_mut() { return get_world_mut().value(); }

    App& add_schedule(schedule::Schedule&& schedule) {
        std::lock_guard lock(_world_mutex);
        app::Schedules& schedules = world_mut().resource_or_init<app::Schedules>();
        // add or replace existing schedule
        schedules.add_schedule(std::move(schedule));
        return *this;
    }
    App& add_systems(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
        schedules_scope([&](app::Schedules& schedules, World& world) mutable {
            schedules.schedule_or_insert((schedule::ScheduleLabel&)schedule).add_systems(std::move(config));
        });
        return *this;
    }
    App& configure_sets(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
        schedules_scope([&](app::Schedules& schedules, World& world) mutable {
            schedules.schedule_or_insert((schedule::ScheduleLabel&)schedule).configure_sets(std::move(config));
        });
        return *this;
    }

    App& schedules_scope(const std::function<void(app::Schedules&, World&)>& func) {
        std::lock_guard lock(_world_mutex);
        auto res = world_mut().resource_scope([&](World& world, app::Schedules& schedules) { func(schedules, world); });
        return *this;
    }
    App& schedule_scope(const schedule::ScheduleLabel& label,
                        const std::function<void(schedule::Schedule&, World&)>& func) {
        schedules_scope([&](app::Schedules& schedules, World& world) {
            auto schedule_opt = schedules.get_schedule_mut(label);
            if (schedule_opt) {
                func(schedule_opt.value().get(), world);
            }
        });
        return *this;
    }

    std::expected<std::shared_ptr<schedule::SystemDispatcher>, WorldNotOwnedError> system_dispatcher() {
        std::lock_guard lock(_world_mutex);
        if (auto dispatcher = _dispatcher.lock()) {
            return dispatcher;
        } else if (_world) {
            auto dispatcher = std::shared_ptr<schedule::SystemDispatcher>(
                new schedule::SystemDispatcher(std::move(_world)), [this](schedule::SystemDispatcher* dispatcher) {
                    // retrieve world back since only the shared ptr have the ability to modify the ptr, it is safe not
                    // having extra synchronization here
                    this->_world = dispatcher->release_world();
                    delete dispatcher;
                });
            _dispatcher = dispatcher;
            return dispatcher;
        }
        return std::unexpected(WorldNotOwnedError{});
    }

    bool run_schedule(const schedule::ScheduleLabel& label) {
        return system_dispatcher()
            .transform([&](std::shared_ptr<schedule::SystemDispatcher> dispatcher) {
                std::optional<schedule::Schedule> schedule;
                dispatcher
                    ->world_scope([&](World& world) {
                        auto schedules_opt = world.get_resource_mut<app::Schedules>();
                        if (schedules_opt) {
                            schedule = schedules_opt.value().get().remove_schedule(label);
                        }
                    })
                    .wait();
                if (schedule) {
                    schedule->execute(*dispatcher);
                    // push back the schedule
                    dispatcher
                        ->world_scope([&](World& world) {
                            auto& schedules = world.resource_or_init<app::Schedules>();
                            if (schedules.get_schedule(label)) {
                                std::println(
                                    std::cerr,
                                    "Warning: schedule with label '{}' was re-added while existing one running",
                                    label.to_string());
                            }
                            schedules.add_schedule(std::move(*schedule));
                        })
                        .wait();
                }
                return schedule.has_value();
            })
            .value_or(false);
    }

   private:
    AppLabel _label = AppLabel::from_type<App>();

    std::recursive_mutex _world_mutex;
    std::unique_ptr<World> _world;
    std::weak_ptr<schedule::SystemDispatcher> _dispatcher;
};
}  // namespace epix::core
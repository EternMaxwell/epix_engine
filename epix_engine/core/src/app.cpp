#include "epix/core/app.hpp"
#include "epix/core/app/app_sche.hpp"
#include "epix/core/app/loop.hpp"

#ifdef EPIX_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#endif

namespace epix::core {

struct DefaultRunner : public AppRunner {
    bool step(App& app) override {
        app.update().get();
        return false;
    }
    void exit(App& app) override { app.run_schedules(app::PreExit, app::Exit, app::PostExit); }
};

App App::create() {
    App app;
    app.add_plugins(app::MainSchedulePlugin{});
    app.set_runner(std::make_unique<DefaultRunner>());
    app.add_event<app::AppExit>();
    return std::move(app);
}

App& App::sub_app_or_insert(const AppLabel& label) {
    auto&& [it, inserted] = _sub_apps.emplace(label, nullptr);
    if (inserted) {
        it->second = std::make_unique<App>(label, _world_ids);
    }
    return *it->second;
}

App& App::add_sub_app(const AppLabel& label) {
    auto&& [it, inserted] = _sub_apps.emplace(label, nullptr);
    if (inserted) {
        it->second = std::make_unique<App>(label, _world_ids);
    }
    return *this;
}

std::optional<std::reference_wrapper<const App>> App::get_sub_app(const AppLabel& label) const {
    auto it = _sub_apps.find(label);
    if (it != _sub_apps.end()) {
        return *it->second;
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<App>> App::get_sub_app_mut(const AppLabel& label) {
    auto it = _sub_apps.find(label);
    if (it != _sub_apps.end()) {
        return *it->second;
    }
    return std::nullopt;
}

std::expected<std::reference_wrapper<const World>, WorldNotOwnedError> App::get_world() const {
    auto lock = lock_world();
    if (_world) {
        return *_world;
    }
    return std::unexpected(WorldNotOwnedError{});
}

std::expected<std::reference_wrapper<World>, WorldNotOwnedError> App::get_world_mut() {
    auto lock = lock_world();
    if (_world) {
        return *_world;
    }
    return std::unexpected(WorldNotOwnedError{});
}

App& App::add_schedule(schedule::Schedule&& schedule) {
    auto lock                 = lock_world();
    app::Schedules& schedules = world_mut().resource_or_init<app::Schedules>();
    // add or replace existing schedule
    schedules.add_schedule(std::move(schedule));
    return *this;
}

App& App::add_systems(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
    std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
    resource_scope([&](app::Schedules& schedules, World& world) mutable {
        schedules.schedule_or_insert((schedule::ScheduleLabel&)schedule).add_systems(std::move(config));
    });
    return *this;
}

App& App::configure_sets(app::ScheduleInfo schedule, schedule::SetConfig&& config) {
    std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
    resource_scope([&](app::Schedules& schedules, World& world) mutable {
        schedules.schedule_or_insert((schedule::ScheduleLabel&)schedule).configure_sets(std::move(config));
    });
    return *this;
}
App& App::configure_sets(schedule::SetConfig&& config) {
    resource_scope([&](app::Schedules& schedules, World& world) mutable {
        for (auto&& [label, schedule] : schedules.iter_mut()) {
            schedule.configure_sets(config.clone());
        }
    });
    return *this;
}

app::Schedules& App::schedules() {
    auto lock = lock_world();
    return world_mut().resource_or_init<app::Schedules>();
}

std::optional<std::reference_wrapper<app::Schedules>> App::get_schedules() {
    auto lock = lock_world();
    return get_world_mut()
        .transform([](auto&& w) { return std::make_optional(w); })
        .value_or(std::nullopt)
        .and_then([](World& world) { return world.get_resource_mut<app::Schedules>(); });
}

ScheduleOrder& App::schedule_order() {
    auto lock = lock_world();
    return world_mut().resource_or_init<ScheduleOrder>();
}

std::optional<std::reference_wrapper<const ScheduleOrder>> App::get_schedule_order() const {
    auto lock = lock_world();
    return get_world()
        .transform([](auto&& w) { return std::make_optional(w); })
        .value_or(std::nullopt)
        .and_then([](const World& world) { return world.get_resource<const ScheduleOrder>(); });
}

App& App::schedule_scope(const schedule::ScheduleLabel& label,
                         const std::function<void(schedule::Schedule&, World&)>& func,
                         bool insert_if_missing) {
    resource_scope([&](app::Schedules& schedules, World& world) {
        if (insert_if_missing) {
            func(schedules.schedule_or_insert(label), world);
        } else {
            schedules.get_schedule_mut(label).transform([&](schedule::Schedule& schedule) {
                func(schedule, world);
                return true;
            });
        }
    });
    return *this;
}

std::expected<std::shared_ptr<schedule::SystemDispatcher>, WorldNotOwnedError> App::get_system_dispatcher() {
    auto lock = lock_world();
    if (auto dispatcher = _dispatcher.lock()) {
        return dispatcher;
    } else if (_world) {
        auto dispatcher = std::shared_ptr<schedule::SystemDispatcher>(
            new schedule::SystemDispatcher(std::move(_world)), [this](schedule::SystemDispatcher* dispatcher) {
                // retrieve world back since only the shared
                // ptr have the ability to modify the ptr, it
                // is safe not having extra synchronization
                // here
                {
                    auto lock    = lock_world();
                    this->_world = dispatcher->release_world();
                }
                delete dispatcher;
            });
        _dispatcher = dispatcher;
        return dispatcher;
    }
    return std::unexpected(WorldNotOwnedError{});
}

bool App::run_schedule(const schedule::ScheduleLabel& label, std::shared_ptr<schedule::SystemDispatcher> dispatcher) {
    std::optional<schedule::Schedule> schedule;
    dispatcher
        ->world_scope([&](World& world) {
            schedule = world.get_resource_mut<app::Schedules>().and_then(
                [&](app::Schedules& schedules) { return schedules.remove_schedule(label); });
        })
        .wait();
    if (schedule) {
        schedule->execute(*dispatcher);
        // push back the schedule
        dispatcher
            ->world_scope([&](World& world) {
                auto& schedules = world.resource_mut<app::Schedules>();
                if (schedules.get_schedule(label)) {
                    spdlog::warn(
                        "Schedule '{}' was re-added while existing one running, old one will be "
                        "overwritten!",
                        label.to_string());
                }
                schedules.add_schedule(std::move(*schedule));
            })
            .wait();
    }
    return schedule.has_value();
}

bool App::update_local(std::shared_ptr<schedule::SystemDispatcher> dispatcher) {
    return dispatcher->world_scope([&](World& world) { return world.take_resource<ScheduleOrder>(); })
        .get()
        .value_or(std::nullopt)
        .transform([this, dispatcher](ScheduleOrder&& order) {
            std::ranges::for_each(order.iter(), [&](const schedule::ScheduleLabel& label) {
                if (!run_schedule(label)) {
                    spdlog::warn("Failed to run schedule '{}', schedule not found.", label.to_string());
                }
            });
            // push back the order
            dispatcher->world_scope([&](World& world) { world.insert_resource(std::move(order)); }).wait();
            return true;
        })
        .value_or(false);
}

void App::extract(App& other) {
    if (extract_fn) {
        std::scoped_lock lock(*_world_mutex, *other._world_mutex);
        auto world_ptr = std::move(other._world);
        world_mut().insert_resource(ExtractedWorld{*world_ptr});
        extract_fn(*this, *world_ptr);
        world_mut().remove_resource<ExtractedWorld>();
        other._world = std::move(world_ptr);
    } else {
        throw std::runtime_error("No extract function set for App.");
    }
}

void App::run() {
    spdlog::info("[app] App building. - {}", _label.to_string());
    resource_scope([&](app::Plugins& plugins) { plugins.finish_all(*this); });
    spdlog::info("[app] App running. - {}", _label.to_string());
    if (!runner) throw std::runtime_error("No runner set for App.");
    while (runner->step(*this)) {
#ifdef EPIX_ENABLE_TRACY
        FrameMark;
#endif
    }
    spdlog::info("[app] App exiting. - {}", _label.to_string());
    runner->exit(*this);
    resource_scope([&](app::Plugins& plugins) { plugins.finalize_all(*this); });
    spdlog::info("[app] App terminated. - {}", _label.to_string());
}
}  // namespace epix::core

module;

#ifdef EPIX_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#endif

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <stacktrace>

module epix.core;

import std;
import epix.meta;

import :app;
import :labels;
import :schedule;

namespace epix::core {

struct DefaultRunner : public AppRunner {
    bool step(App& app) override {
        app.update();
        return false;
    }
    void exit(App& app) override { app.run_schedules(PreExit, Exit, PostExit); }
};

App App::create() {
    static std::once_flag spdlog_env_loaded;
    std::call_once(spdlog_env_loaded, []() { spdlog::cfg::load_env_levels(); });
    return App(DefaultCreateTag{});
}

App::App(DefaultCreateTag,
         const AppLabel& label,
         std::shared_ptr<TypeRegistry> type_registry,
         std::shared_ptr<std::atomic<uint32_t>> world_ids)
    : App(label, std::move(type_registry), std::move(world_ids)) {
    add_plugins(MainSchedulePlugin{});
    set_runner(std::make_unique<DefaultRunner>());
    add_event<AppExit>();
}

App& App::sub_app_or_insert(const AppLabel& label) {
    auto&& [it, inserted] = _sub_apps.emplace(label, nullptr);
    if (inserted) {
        spdlog::debug("[app] Created sub-app '{}' for parent '{}'.", label.to_string(), _label.to_string());
        it->second = std::make_unique<App>(label, world().type_registry_ptr(), _world_ids);
    }
    return *it->second;
}

App& App::add_sub_app(const AppLabel& label) {
    auto&& [it, inserted] = _sub_apps.emplace(label, nullptr);
    if (inserted) {
        it->second = std::make_unique<App>(label, world().type_registry_ptr(), _world_ids);
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

App& App::add_schedule(Schedule&& schedule) {
    spdlog::trace("[app] Adding schedule '{}' to app '{}'.", schedule.label().to_string(), _label.to_string());
    Schedules& schedules = _world.resource_or_init<Schedules>();
    // add or replace existing schedule
    schedules.add_schedule(std::move(schedule));
    return *this;
}

App& App::add_systems(ScheduleInfo schedule, SetConfig&& config) {
    std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
    resource_scope([&](Schedules& schedules, World& world) mutable {
        schedules.schedule_or_insert((ScheduleLabel&)schedule).add_systems(std::move(config));
    });
    return *this;
}

App& App::add_pre_systems(ScheduleInfo schedule, SetConfig&& config) {
    std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
    resource_scope([&](Schedules& schedules, World& world) mutable {
        schedules.schedule_or_insert((ScheduleLabel&)schedule).add_pre_systems(std::move(config));
    });
    return *this;
}

App& App::add_pre_systems(SetConfig&& config) {
    resource_scope([&](Schedules& schedules, ScheduleOrder& order) mutable {
        for (const auto& label : order.iter()) {
            schedules.schedule_or_insert(label).add_pre_systems(config.clone());
        }
    });
    return *this;
}

App& App::add_post_systems(ScheduleInfo schedule, SetConfig&& config) {
    std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
    resource_scope([&](Schedules& schedules, World& world) mutable {
        schedules.schedule_or_insert((ScheduleLabel&)schedule).add_post_systems(std::move(config));
    });
    return *this;
}

App& App::add_post_systems(SetConfig&& config) {
    resource_scope([&](Schedules& schedules, ScheduleOrder& order) mutable {
        for (const auto& label : order.iter()) {
            schedules.schedule_or_insert(label).add_post_systems(config.clone());
        }
    });
    return *this;
}

App& App::configure_sets(ScheduleInfo schedule, SetConfig&& config) {
    std::ranges::for_each(schedule.transforms, [&](auto&& transform) { transform(config); });
    resource_scope([&](Schedules& schedules, World& world) mutable {
        schedules.schedule_or_insert((ScheduleLabel&)schedule).configure_sets(std::move(config));
    });
    return *this;
}
App& App::configure_sets(SetConfig&& config) {
    resource_scope([&](Schedules& schedules, World& world) mutable {
        for (auto&& [label, schedule] : schedules.iter_mut()) {
            schedule.configure_sets(config.clone());
        }
    });
    return *this;
}

Schedules& App::schedules() { return _world.resource_or_init<Schedules>(); }

std::optional<std::reference_wrapper<Schedules>> App::get_schedules() { return _world.get_resource_mut<Schedules>(); }

ScheduleOrder& App::schedule_order() { return _world.resource_or_init<ScheduleOrder>(); }

std::optional<std::reference_wrapper<const ScheduleOrder>> App::get_schedule_order() const {
    return _world.get_resource<const ScheduleOrder>();
}

App& App::schedule_scope(const ScheduleLabel& label,
                         const std::function<void(Schedule&, World&)>& func,
                         bool insert_if_missing) {
    resource_scope([&](Schedules& schedules, World& world) {
        if (insert_if_missing) {
            func(schedules.schedule_or_insert(label), world);
        } else {
            schedules.get_schedule_mut(label).transform([&](Schedule& schedule) {
                func(schedule, world);
                return true;
            });
        }
    });
    return *this;
}

bool App::run_schedule(const ScheduleLabel& label) {
    auto schedules_opt = _world.get_resource_mut<Schedules>();
    if (!schedules_opt) return false;
    auto schedule = schedules_opt->get().remove_schedule(label);
    if (!schedule) return false;
    spdlog::trace("[app] Executing schedule '{}'.", label.to_string());
    schedule->execute(_world);
    if (schedules_opt->get().get_schedule(label)) {
        spdlog::warn(
            "Schedule '{}' was re-added while existing one running, old one will be "
            "overwritten!",
            label.to_string());
    }
    schedules_opt->get().add_schedule(std::move(*schedule));
    return true;
}

void App::update() {
    spdlog::trace("[app] Update tick for '{}'.", _label.to_string());
    _world.check_change_tick(
        [&](Tick tick) { _world.resource_scope([&](Schedules& schedules) { schedules.check_change_tick(tick); }); });
    auto order_opt = _world.take_resource<ScheduleOrder>();
    if (order_opt) {
        for (const auto& label : order_opt->iter()) {
            if (!run_schedule(label)) {
                spdlog::error("Failed to run schedule '{}', schedule not found.", label.to_string());
            }
        }
        _world.insert_resource(std::move(*order_opt));
    }
}

std::unique_ptr<App> App::take_sub_app(const AppLabel& label) {
    auto it = _sub_apps.find(label);
    if (it != _sub_apps.end()) {
        auto app = std::move(it->second);
        _sub_apps.erase(it);
        return app;
    }
    return nullptr;
}

void App::insert_sub_app(const AppLabel& label, std::unique_ptr<App> app) { _sub_apps[label] = std::move(app); }

void App::extract(App& other) {
    if (extract_fn) {
        spdlog::trace("[app] Extracting from app '{}' to '{}'.", other._label.to_string(), _label.to_string());
        _world.insert_resource(ExtractedWorld{other._world});
        extract_fn(*this, other._world);
        _world.remove_resource<ExtractedWorld>();
    }
}

void handle_terminate() {
    std::stacktrace stack                = std::stacktrace::current();
    std::exception_ptr current_exception = std::current_exception();
    if (current_exception) {
        try {
            std::rethrow_exception(current_exception);
        } catch (const std::exception& e) {
            spdlog::error("Unhandled exception: {}, with stack:\n{}", e.what(), stack);
        } catch (...) {
            spdlog::error("Unhandled unknown exception with stack:\n{}", stack);
        }
    } else {
        spdlog::error("Terminated without exception, with stack:\n{}", stack);
    }
    std::exit(1);
}

void App::run() {
    auto prev_terminate = std::set_terminate(handle_terminate);
    auto file_sink      = std::make_shared<spdlog::sinks::basic_file_sink_mt>("epix.log", true);
    spdlog::default_logger()->sinks().push_back(file_sink);
    spdlog::info("[app] App building. - {}", _label.to_string());
    resource_scope([&](Plugins& plugins) {
        spdlog::debug("[app] Finishing all plugins for '{}'.", _label.to_string());
        plugins.finish_all(*this);
    });
    resource_scope([&](World& world, Schedules& schedules) {
        spdlog::debug("[app] Preparing and initializing schedules for '{}'.", _label.to_string());
        for (auto&& [label, schedule] : schedules.iter_mut()) {
            spdlog::trace("[app] Preparing schedule '{}'.", label.to_string());
            auto res = schedule.prepare(false);
            schedule.initialize_systems(world);
        }
    });
    spdlog::info("[app] App running. - {}", _label.to_string());
    if (!runner) throw std::runtime_error("No runner set for App.");
    while (runner->step(*this)) {
#ifdef EPIX_ENABLE_TRACY
        FrameMark;
#endif
    }
    spdlog::info("[app] App exiting. - {}", _label.to_string());
    runner->exit(*this);
    resource_scope([&](Plugins& plugins) { plugins.finalize_all(*this); });
    spdlog::info("[app] App terminated. - {}", _label.to_string());
    std::set_terminate(prev_terminate);
    auto& sinks = spdlog::default_logger()->sinks();
    sinks.erase(std::remove(sinks.begin(), sinks.end(), file_sink), sinks.end());
}
}  // namespace epix::core

#include "epix/app/app.h"

using namespace epix::app;

EPIX_API void EventSystem::finish(App& app) {
    app.add_systems(
        Last, into([updates = std::move(updates)](World& world) {
                  static BS::thread_pool<BS::tp::none> event_pool(16);
                  for (auto&& sub_range : std::views::chunk(updates, 4)) {
                      event_pool.detach_task([&world, sub_range]() {
                          for (auto&& update : sub_range) {
                              update(world);
                          }
                      });
                  }
                  event_pool.wait();
              }).set_name("update events")
    );
}

struct OnceRunner : public AppRunner {
    int run(App& app) override {
        app.update().get();
        spdlog::info("[app] Exiting app.");
        app.exit().get();
        spdlog::info("[app] App terminated.");
        return 0;
    }
};

EPIX_API AppData::AppData(const AppLabel& world_label)
    : label(world_label), world(world_label) {}

template <typename... Labels>
void add_schedules(App& app, Labels&&... labels) {
    (app.add_schedule(labels), ...);
}
template <typename... Labels>
void add_run_once_schedules(App& app, Labels&&... labels) {
    auto add = [&app](const ScheduleLabel& label) {
        Schedule sche(label);
        sche.config.run_once = true;
        app.add_schedule(std::move(sche));
    };
    (add(labels), ...);
}

static BS::thread_pool<BS::tp::none> app_run_pool(4, []() {
    // set thread name
    BS::this_thread::set_os_thread_name("app");
});

EPIX_API App::App(const AppLabel& label) : m_data(new AppData(label)) {}
EPIX_API App::~App() {
    if (m_data) {
        // to wait for any other taskes to finish
        // just wait for world.
        app_run_pool.wait();
        auto write_world = world();
    }
}

EPIX_API App App::create(const AppCreateInfo& create_info) {
    App app(Main);
    add_schedules(
        app, First, PreUpdate, StateTransition, Update, PostUpdate, Last
    );
    add_run_once_schedules(app, PreStartup, Startup, PostStartup);
    add_schedules(app, PreExit, Exit, PostExit);
    {
        auto main_order = app.m_data->main_schedule_order.write();
        main_order->insert(
            main_order->end(),
            {PreStartup, Startup, PostStartup, First, PreUpdate,
             StateTransition, Update, PostUpdate, Last}
        );
    }
    {
        auto exit_order = app.m_data->exit_schedule_order.write();
        exit_order->insert(exit_order->end(), {PreExit, Exit, PostExit});
    }
    app.m_executors = std::make_shared<Executors>();
    app.m_executors->add_pool(ExecutorType::SingleThread, "single", 1);
    app.m_executors->add_pool(
        ExecutorType::MultiThread, "multi", create_info.default_pool_size
    );
    return std::move(app);
}

EPIX_API epix::async::RwLock<World>::WriteGuard App::world() {
    reset_run_state();
    return m_data->world.write();
};
EPIX_API epix::async::RwLock<World>::ReadGuard App::world() const {
    reset_run_state();
    return m_data->world.read();
}

EPIX_API Schedule& App::schedule(const ScheduleLabel& label) {
    auto write = m_data->schedules.write();
    if (write->contains(label)) {
        return *write->at(label);
    }
    throw std::runtime_error("Schedule not found: " + label.name());
};
EPIX_API Schedule* App::get_schedule(const ScheduleLabel& label) {
    auto read = m_data->schedules.read();
    if (read->contains(label)) {
        return read->at(label).get();
    }
    return nullptr;
};
EPIX_API App& App::sub_app(const AppLabel& label) {
    auto write = m_sub_apps.write();
    if (write->contains(label)) {
        return write->at(label);
    }
    throw std::runtime_error("Sub app not found: " + label.name());
};
EPIX_API App* App::get_sub_app(const AppLabel& label) {
    auto read = m_sub_apps.write();
    if (read->contains(label)) {
        return &read->at(label);
    }
    return nullptr;
};
EPIX_API App& App::add_sub_app(const AppLabel& label) {
    auto write      = m_sub_apps.write();
    auto& sub       = write->emplace(label, App(label)).first->second;
    sub.m_executors = m_executors;
    return *this;
};
EPIX_API std::shared_ptr<RunState> App::run_state() const {
    auto write = m_data->run_state.write();
    if (*write) return *write;
    // create a new RunState if not exists
    auto world     = m_data->world.write();
    auto executors = m_executors.get();
    if (!executors) {
        throw std::runtime_error("Executors not initialized.");
    }
    auto run_state = std::make_shared<RunState>(std::move(world), *executors);
    *write         = run_state;
    return run_state;
};
EPIX_API void App::reset_run_state() const {
    auto write = m_data->run_state.write();
    *write     = nullptr;
};
EPIX_API App& App::add_schedule(Schedule&& schedule) {
    Schedule* pschedule = nullptr;
    {
        auto label = schedule.label();
        auto write = m_data->schedules.write();
        if (write->contains(label)) {
            // error to be handled: schedule already exists
            return *this;
        }
        pschedule =
            write
                ->emplace(
                    label, std::make_unique<Schedule>(std::move(schedule))
                )
                .first->second.get();
    }
    auto queued_all_sets = m_data->queued_all_sets.write();
    for (auto&& config : *queued_all_sets) {
        pschedule->configure_sets(config);
    }
    return *this;
};
EPIX_API App& App::add_schedule(const ScheduleLabel& label) {
    add_schedule(Schedule(label));
    return *this;
};
EPIX_API App& App::main_schedule_order(
    const ScheduleLabel& left, std::optional<ScheduleLabel> right
) {
    auto write = m_data->main_schedule_order.write();
    if (!right) {
        write->insert(write->begin(), left);
    } else {
        // add right after left
        // if left not set, ignore
        if (auto it = std::find(write->begin(), write->end(), left);
            it != write->end()) {
            write->insert(it + 1, *right);
        } else if (auto it =
                       std::find(write->begin(), write->end(), right.value());
                   it != write->end()) {
            write->insert(it, left);
        }
    }
    return *this;
};
EPIX_API App& App::exit_schedule_order(
    const ScheduleLabel& left, std::optional<ScheduleLabel> right
) {
    auto write = m_data->exit_schedule_order.write();
    if (!right) {
        write->insert(write->begin(), left);
    } else {
        if (auto it = std::find(write->begin(), write->end(), left);
            it != write->end()) {
            // add right after left
            write->insert(it + 1, *right);
        } else if (auto it =
                       std::find(write->begin(), write->end(), right.value());
                   it != write->end()) {
            // add left before right
            write->insert(it, left);
        }
    }
    return *this;
};
EPIX_API App& App::extract_schedule_order(
    const ScheduleLabel& left, std::optional<ScheduleLabel> right
) {
    auto write = m_data->extract_schedule_order.write();
    if (!right) {
        write->insert(write->begin(), left);
    } else {
        if (auto it = std::find(write->begin(), write->end(), left);
            it != write->end()) {
            // add right after left
            write->insert(it + 1, *right);
        } else if (auto it =
                       std::find(write->begin(), write->end(), right.value());
                   it != write->end()) {
            // add left before right
            write->insert(it, left);
        }
    }
    return *this;
};

EPIX_API App& App::add_systems(
    const ScheduleInfo& info, SystemSetConfig&& config
) {
    {
        auto write = m_data->schedules.write();
        if (write->contains(info)) {
            auto& schedule = write->at(info);
            schedule->add_systems(std::move(config));
            return *this;
        }
    }
    auto write = m_data->queued_systems.write();
    write->emplace_back(info, std::move(config));
    return *this;
};
EPIX_API App& App::add_systems(
    const ScheduleInfo& label, SystemSetConfig& config
) {
    add_systems(label, std::move(config));
    return *this;
};
EPIX_API App& App::configure_sets(
    const ScheduleLabel& label, const SystemSetConfig& config
) {
    {
        auto write = m_data->schedules.write();
        if (write->contains(label)) {
            auto& schedule = write->at(label);
            schedule->configure_sets(config);
        }
        return *this;
    }
    auto write = m_data->queued_sets.write();
    write->emplace_back(label, config);
    return *this;
};
EPIX_API App& App::configure_sets(const SystemSetConfig& config) {
    {
        auto write = m_data->schedules.write();
        for (auto&& [schedule_label, schedule] : *write) {
            schedule->configure_sets(config);
        }
    }
    auto write = m_data->queued_all_sets.write();
    write->emplace_back(config);
    return *this;
}
EPIX_API App& App::remove_system(
    const ScheduleLabel& id, const SystemSetLabel& label
) {
    auto write = m_data->schedules.write();
    if (write->contains(id)) {
        auto& schedule = write->at(id);
        schedule->remove_system(label);
    } else {
        // error to be handled: schedule not found
        return *this;
    }
    return *this;
};
EPIX_API App& App::remove_set(
    const ScheduleLabel& id, const SystemSetLabel& label
) {
    auto write = m_data->schedules.write();
    if (write->contains(id)) {
        auto& schedule = write->at(id);
        schedule->remove_set(label);
    } else {
        // error to be handled: schedule not found
        return *this;
    }
    return *this;
};
EPIX_API App& App::remove_set(const SystemSetLabel& label) {
    auto write = m_data->schedules.write();
    for (auto&& [schedule_label, schedule] : *write) {
        schedule->remove_set(label);
    }
    return *this;
};

EPIX_API void App::build() {
    // build plugins
    std::vector<Plugin*> to_build;
    size_t last_index = 0;
    do {
        for (auto&& plugin : to_build) {
            plugin->build(*this);
        }
        to_build.clear();
        {
            auto pplugins = m_data->plugins.write();
            auto& plugins = *pplugins;
            for (size_t i = last_index; i < plugins.size(); ++i) {
                to_build.push_back(plugins[i].second.get());
            }
            last_index = plugins.size();
        }
    } while (!to_build.empty());
    {
        auto pplugins = m_data->plugins.write();
        auto& plugins = *pplugins;
        for (auto&& [id, plugin] : plugins) {
            to_build.push_back(plugin.get());
        }
    }
    for (auto&& plugin : to_build) {
        plugin->finish(*this);
    }
    {
        auto pplugins = m_data->plugins.write();
        auto& plugins = *pplugins;
        for (auto&& [id, plugin] : plugins) {
            auto w = world();
            w->add_resource(id, plugin);
        }
    }

    // retry to add queued systems
    {
        auto m_queued_systems = m_data->queued_systems.write();
        for (auto&& [info, config] : *m_queued_systems) {
            add_systems(info, std::move(config));
        }
    }
    // retry to add queued sets
    {
        auto m_queued_sets = m_data->queued_sets.write();
        for (auto&& [label, config] : *m_queued_sets) {
            configure_sets(label, config);
        }
    }

    // collect all schedules and put their references in the resources
    auto schedules_res = std::make_shared<Schedules>();
    auto pschedules    = m_data->schedules.write();
    auto& schedules    = *pschedules;
    for (auto&& [schedule_label, schedule] : schedules) {
        schedules_res->emplace(schedule_label, schedule.get());
    }
    auto world = this->world();
    world->add_resource(std::move(schedules_res));
}

EPIX_API void App::set_runner(std::unique_ptr<AppRunner>&& runner) {
    auto write = m_runner.write();
    *write     = std::move(runner);
}

EPIX_API std::future<void> App::extract(App& target) {
    auto schedules = m_data->schedules.write();
    return app_run_pool.submit_task([this, &target,
                                     schedules =
                                         std::move(schedules)]() mutable {
        auto extract_order = m_data->extract_schedule_order.read();
        auto reset_target  = IntoSystem::into_system([](World& world) {
            world.remove_resource<ExtractTarget>();
        });
        {
            auto target_world = target.world();
            auto source_world = this->world();
            if (auto extract_target =
                    source_world->get_resource<ExtractTarget>()) {
                extract_target->m_world = &(*target_world);
            } else {
                source_world->insert_resource(ExtractTarget(*target_world));
            }
            reset_target->initialize(*source_world);
            for (auto&& label : *extract_order) {
                schedules->at(label)->initialize_systems(*source_world);
            }
        }
        auto run_state = this->run_state();
        for (auto&& label : *extract_order) {
            auto& schedule                = schedules->at(label);
            schedule->config.enable_tracy = config.enable_tracy;
            schedule->run(*run_state).wait();
        }
        run_state->apply_commands();
        run_state->run_system(
            reset_target.get(),
            RunState::RunSystemConfig{.executor = ExecutorType::SingleThread}
        );
        run_state->wait();
    });
}
EPIX_API std::future<void> App::update() {
    auto schedules = m_data->schedules.write();
    return app_run_pool.submit_task([this, schedules =
                                               std::move(schedules)]() mutable {
        auto update_order = m_data->main_schedule_order.read();
        auto reset_target = IntoSystem::into_system([](World& world) {
            world.remove_resource<ExtractTarget>();
        });
        {
            auto world = this->world();
            if (auto extract_target = world->get_resource<ExtractTarget>()) {
                extract_target->m_world = &(*world);
            } else {
                world->insert_resource(ExtractTarget(*world));
            }
            reset_target->initialize(*world);
            for (auto&& label : *update_order) {
                auto& schedule = schedules->at(label);
                schedule->initialize_systems(*world);
            }
        }
        auto run_state = this->run_state();
        for (auto&& label : *update_order) {
            auto& schedule                = schedules->at(label);
            schedule->config.enable_tracy = config.enable_tracy;
            schedule->run(*run_state).wait();
        }
        run_state->apply_commands();
        run_state->run_system(
            reset_target.get(),
            RunState::RunSystemConfig{.executor = ExecutorType::SingleThread}
        );
        run_state->wait();
    });
}
EPIX_API std::future<void> App::exit() {
    auto schedules = m_data->schedules.write();
    return app_run_pool.submit_task([this, schedules =
                                               std::move(schedules)]() mutable {
        auto exit_order   = m_data->exit_schedule_order.read();
        auto reset_target = IntoSystem::into_system([](World& world) {
            world.remove_resource<ExtractTarget>();
        });
        {
            auto world = this->world();
            if (auto extract_target = world->get_resource<ExtractTarget>()) {
                extract_target->m_world = &(*world);
            } else {
                world->insert_resource(ExtractTarget(*world));
            }
            reset_target->initialize(*world);
            for (auto&& label : *exit_order) {
                auto& schedule = schedules->at(label);
                schedule->initialize_systems(*world);
            }
        }
        auto run_state = this->run_state();
        for (auto&& label : *exit_order) {
            auto& schedule                = schedules->at(label);
            schedule->config.enable_tracy = config.enable_tracy;
            schedule->run(*run_state).wait();
        }
        run_state->apply_commands();
        run_state->run_system(
            reset_target.get(),
            RunState::RunSystemConfig{.executor = ExecutorType::SingleThread}
        );
        run_state->wait();
    });
}

EPIX_API int App::run() {
    spdlog::info("[app] Building app.");
    add_events<AppExit>();
    build();
    spdlog::info("[app] Running app.");
    auto profiler = std::make_shared<AppProfiler>();
    {
        auto clone = profiler;
        add_resource(std::move(clone));
        // add to sub apps
        auto sub_apps = m_sub_apps.write();
        for (auto&& [label, sub_app] : *sub_apps) {
            auto clone = profiler;
            sub_app.add_resource(std::move(clone));
        }
    }
    auto write   = m_runner.write();
    auto& runner = *write;
    if (!runner) {
        runner = std::make_unique<OnceRunner>();
    }
    return runner->run(*this);
}

EPIX_API Schedule* Schedules::get(const ScheduleLabel& label) {
    auto it = find(label);
    if (it != end()) {
        return it->second;
    }
    return nullptr;
};
EPIX_API const Schedule* Schedules::get(const ScheduleLabel& label) const {
    auto it = find(label);
    if (it != end()) {
        return it->second;
    }
    return nullptr;
}
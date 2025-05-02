#include "epix/app/app.h"

using namespace epix::app;

EPIX_API ScheduleConfig::ScheduleConfig(Schedule&& schedule)
    : label(schedule.label),
      schedule(std::move(schedule)),
      src_world(MainWorld),
      dst_world(MainWorld) {}
EPIX_API ScheduleConfig::ScheduleConfig(ScheduleLabel label)
    : label(label), src_world(MainWorld), dst_world(MainWorld) {}
EPIX_API ScheduleConfig& ScheduleConfig::after(ScheduleLabel label) {
    depends.emplace(label);
    return *this;
};
EPIX_API ScheduleConfig& ScheduleConfig::before(ScheduleLabel label) {
    succeeds.emplace(label);
    return *this;
};
EPIX_API ScheduleConfig& ScheduleConfig::set_src(WorldLabel label) {
    src_world = label;
    return *this;
};
EPIX_API ScheduleConfig& ScheduleConfig::set_dst(WorldLabel label) {
    dst_world = label;
    return *this;
};
EPIX_API ScheduleConfig& ScheduleConfig::set_run_once() {
    run_once = true;
    return *this;
};

struct OnceRunner : public AppRunner {
    int run(App& app) override {
        app.run_group(LoopGroup);
        app.logger()->info("Exiting app.");
        app.run_group(ExitGroup);
        app.logger()->info("App terminated.");
        return 0;
    }
};

template <typename... Labels>
void add_schedules(App& app, GroupLabel group, Labels&&... labels) {
    (app.add_schedule(group, ScheduleConfig(labels)), ...);
}
template <typename... Labels>
void add_run_once_schedules(App& app, GroupLabel group, Labels&&... labels) {
    (app.add_schedule(group, ScheduleConfig(labels).set_run_once()), ...);
}
template <typename... Labels>
void schedules_set_src_world(App& app, WorldLabel label, Labels&&... labels) {
    (app.schedule_set_src(labels, label), ...);
}
template <typename... Labels>
void schedules_set_dst_world(App& app, WorldLabel label, Labels&&... labels) {
    (app.schedule_set_dst(labels, label), ...);
}

EPIX_API App::App(const AppCreateInfo& create_info)
    : m_executors(std::make_shared<Executors>()),
      m_control_pool(std::make_shared<executor_t>(
          create_info.control_pool_size,
          []() { BS::this_thread::set_os_thread_name("control"); }
      )),
      m_mutex(std::make_unique<std::shared_mutex>()),
      m_logger(spdlog::default_logger()->clone("app")) {
    add_world(MainWorld);
    add_schedule_group(LoopGroup);
    add_schedule_group(ExitGroup);
    add_schedules(
        *this, LoopGroup, First, PreUpdate, StateTransition, Update, PostUpdate,
        Last
    );
    add_run_once_schedules(*this, LoopGroup, PreStartup, Startup, PostStartup);
    add_schedules(*this, ExitGroup, PreExit, Exit, PostExit);
    schedule_sequence(
        PreStartup, Startup, PostStartup, First, PreUpdate, StateTransition,
        Update, PostUpdate, Last
    );
    schedule_sequence(PreExit, Exit, PostExit);

    m_executors->add_pool(
        ExecutorLabel(), "default", create_info.default_pool_size
    );
    m_executors->add_pool(ExecutorType::SingleThread, "single", 1);

    m_tracy_settings.enable_tracy = create_info.enable_tracy;
    m_tracy_settings.mark_frame   = create_info.mark_frame;
}

EPIX_API App App::create(const AppCreateInfo& create_info) {
    App app(create_info);
    app.add_world(RenderWorld);
    add_schedules(
        app, LoopGroup, Prepare, PreRender, Render, PostRender, PreExtract,
        Extraction, PostExtract
    );
    app.schedule_sequence(
        PostStartup, PreExtract, Extraction, PostExtract, Prepare, PreRender,
        Render, PostRender
    );
    app.schedule_sequence(PostExtract, First);
    schedules_set_dst_world(
        app, RenderWorld, Prepare, PreRender, Render, PostRender, PreExtract,
        Extraction, PostExtract
    );
    schedules_set_src_world(
        app, RenderWorld, Prepare, PreRender, Render, PostRender
    );
    return app;
}

EPIX_API App& App::set_logger(std::shared_ptr<spdlog::logger> logger) {
    m_logger = logger->clone("app");
    for (auto&& [label, group] : m_schedule_groups) {
        for (auto&& [schedule_label, schedule] : group.schedules) {
            schedule->set_logger(m_logger);
        }
    }
    return *this;
};
EPIX_API std::shared_ptr<spdlog::logger> App::logger() { return m_logger; };

EPIX_API std::optional<GroupLabel> App::find_belonged_group(
    const ScheduleLabel& label
) const noexcept {
    std::shared_lock lock(*m_mutex);
    for (auto&& [schedules_label, group] : m_schedule_groups) {
        if (group.schedules.contains(label)) {
            return schedules_label;
        }
    }
    return std::nullopt;
}

EPIX_API World& App::world(const WorldLabel& label) {
    std::shared_lock lock(*m_mutex);
    auto it = m_worlds.find(label);
    if (it != m_worlds.end()) {
        return *it->second;
    }
    throw std::runtime_error("World not found.");
};
EPIX_API World* App::get_world(const WorldLabel& label) {
    std::shared_lock lock(*m_mutex);
    auto it = m_worlds.find(label);
    if (it != m_worlds.end()) {
        return it->second.get();
    }
    return nullptr;
};
EPIX_API Executors& App::executors() {
    std::shared_lock lock(*m_mutex);
    if (m_executors) {
        return *m_executors;
    }
    throw std::runtime_error("Executors not found.");
};
EPIX_API std::shared_ptr<Executors> App::get_executors() {
    std::shared_lock lock(*m_mutex);
    return m_executors;
};
EPIX_API App::executor_t& App::control_pool() {
    std::shared_lock lock(*m_mutex);
    if (m_control_pool) {
        return *m_control_pool;
    }
    throw std::runtime_error("Control pool not found.");
};
EPIX_API std::shared_ptr<App::executor_t> App::get_control_pool() {
    std::shared_lock lock(*m_mutex);
    return m_control_pool;
};
EPIX_API Schedule& App::schedule(const ScheduleLabel& label) {
    std::shared_lock lock(*m_mutex);
    for (auto&& [schedules_label, group] : m_schedule_groups) {
        if (group.schedules.contains(label)) {
            return *group.schedules.at(label);
        }
    }
    throw std::runtime_error("Schedule not found.");
};
EPIX_API Schedule* App::get_schedule(const ScheduleLabel& label) {
    std::shared_lock lock(*m_mutex);
    for (auto&& [schedules_label, group] : m_schedule_groups) {
        if (group.schedules.contains(label)) {
            return group.schedules.at(label).get();
        }
    }
    return nullptr;
};
EPIX_API ScheduleGroup& App::schedule_group(const GroupLabel& label) {
    std::shared_lock lock(*m_mutex);
    auto it = m_schedule_groups.find(label);
    if (it != m_schedule_groups.end()) {
        return it->second;
    }
    throw std::runtime_error("Schedule group not found.");
};
EPIX_API ScheduleGroup* App::get_schedule_group(const GroupLabel& label) {
    std::shared_lock lock(*m_mutex);
    auto it = m_schedule_groups.find(label);
    if (it != m_schedule_groups.end()) {
        return &it->second;
    }
    return nullptr;
};

EPIX_API App& App::add_world(const WorldLabel& label) {
    std::unique_lock lock(*m_mutex);
    m_worlds.emplace(label, std::make_unique<World>());
    return *this;
};
EPIX_API App& App::add_schedule_group(
    const GroupLabel& label, ScheduleGroup&& group
) {
    std::unique_lock lock(*m_mutex);
    m_schedule_groups.emplace(label, std::move(group));
    return *this;
};
EPIX_API App& App::add_schedule(
    const GroupLabel& label, ScheduleConfig&& config
) {
    if (find_belonged_group(config.label)) {
        // error to be handled: schedule already exists
        return *this;
    }
    std::unique_lock lock(*m_mutex);
    if (!m_schedule_groups.contains(label)) {
        m_schedule_groups.emplace(label, ScheduleGroup{});
    }
    auto&& group = m_schedule_groups.at(label);
    if (config.schedule) {
        group.schedules.emplace(
            config.label,
            std::make_unique<Schedule>(std::move(*config.schedule))
        );
    } else {
        group.schedules.emplace(
            config.label, std::make_unique<Schedule>(config.label)
        );
    }
    group.schedules.at(config.label)->set_logger(m_logger);
    group.schedule_src.emplace(config.label, config.src_world);
    group.schedule_dst.emplace(config.label, config.dst_world);
    group.schedule_nodes[config.label].depends  = config.depends;
    group.schedule_nodes[config.label].succeeds = config.succeeds;
    group.schedule_run_once.emplace(config.label, config.run_once);
    for (auto& set_config : m_queued_all_sets) {
        group.schedules.at(config.label)->configure_sets(set_config);
    }
    return *this;
};
EPIX_API App& App::add_schedule(
    const GroupLabel& label, ScheduleConfig& config
) {
    add_schedule(label, std::move(config));
    return *this;
};
EPIX_API App& App::schedule_sequence(
    const ScheduleLabel& scheduleId, const ScheduleLabel& otherId
) {
    auto group1 = find_belonged_group(scheduleId);
    auto group2 = find_belonged_group(otherId);
    if (!group1 || !group2) {
        // error to be handled: non of the provided schedules is found
        return *this;
    }
    std::unique_lock lock(*m_mutex);
    if (*group1 != *group2) {
        // error to be handled: 2 schedules are not in the same group
        return *this;
    }
    auto label = *group1;
    if (auto&& it = m_schedule_groups.find(label);
        it != m_schedule_groups.end()) {
        bool found_any = false;
        if (auto&& node_it = it->second.schedule_nodes.find(scheduleId);
            node_it != it->second.schedule_nodes.end()) {
            auto&& node = node_it->second;
            node.succeeds.emplace(otherId);
            found_any = true;
        }
        if (auto&& node_it = it->second.schedule_nodes.find(otherId);
            node_it != it->second.schedule_nodes.end()) {
            auto&& node = node_it->second;
            node.depends.emplace(scheduleId);
            found_any = true;
        }
    }
    return *this;
};

EPIX_API App& App::schedule_set_src(
    const ScheduleLabel& scheduleId, const WorldLabel& worldId
) {
    auto group_opt = find_belonged_group(scheduleId);
    if (!group_opt) {
        // error to be handled: schedule not found
        return *this;
    }
    std::unique_lock lock(*m_mutex);
    auto group_label               = *group_opt;
    auto&& group                   = m_schedule_groups.at(group_label);
    group.schedule_src[scheduleId] = worldId;
    return *this;
};
EPIX_API App& App::schedule_set_dst(
    const ScheduleLabel& scheduleId, const WorldLabel& worldId
) {
    auto group_opt = find_belonged_group(scheduleId);
    if (!group_opt) {
        // error to be handled: schedule not found
        return *this;
    }
    std::unique_lock lock(*m_mutex);
    auto group_label               = *group_opt;
    auto&& group                   = m_schedule_groups.at(group_label);
    group.schedule_dst[scheduleId] = worldId;
    return *this;
};

EPIX_API App& App::add_systems(
    const ScheduleInfo& label, SystemConfig&& config
) {
    std::shared_lock lock(*m_mutex);
    auto group_opt = find_belonged_group(label);
    if (!group_opt) {
        m_queued_systems.emplace_back(label, std::move(config));
        return *this;
    }
    for (auto&& transform : label.transforms) {
        transform(config);
    }
    auto group_label = *group_opt;
    auto&& group     = m_schedule_groups.at(group_label);
    auto&& schedule  = group.schedules.at(label);
    schedule->add_systems(std::move(config));
    return *this;
};
EPIX_API App& App::add_systems(
    const ScheduleInfo& label, SystemConfig& config
) {
    add_systems(label, std::move(config));
    return *this;
};
EPIX_API App& App::configure_sets(
    const ScheduleLabel& label, const SystemSetConfig& config
) {
    std::shared_lock lock(*m_mutex);
    auto group_opt = find_belonged_group(label);
    if (!group_opt) {
        m_queued_sets.emplace_back(label, config);
        return *this;
    }
    auto group_label = *group_opt;
    auto&& group     = m_schedule_groups.at(group_label);
    auto&& schedule  = group.schedules.at(label);
    schedule->configure_sets(config);
    return *this;
};
EPIX_API App& App::configure_sets(const SystemSetConfig& config) {
    std::shared_lock lock(*m_mutex);
    for (auto&& [schedules_label, group] : m_schedule_groups) {
        for (auto&& [schedule_label, schedule] : group.schedules) {
            schedule->configure_sets(config);
        }
    }
    m_queued_all_sets.emplace_back(config);
    return *this;
}
EPIX_API App& App::remove_system(
    const ScheduleLabel& id, const SystemLabel& label
) {
    std::shared_lock lock(*m_mutex);
    auto group_opt = find_belonged_group(id);
    if (!group_opt) {
        // error to be handled: schedule not found
        return *this;
    }
    auto group_label = *group_opt;
    auto&& group     = m_schedule_groups.at(group_label);
    auto&& schedule  = group.schedules.at(id);
    schedule->remove_system(label);
    return *this;
};
EPIX_API App& App::remove_set(
    const ScheduleLabel& id, const SystemSetLabel& label
) {
    std::shared_lock lock(*m_mutex);
    auto group_opt = find_belonged_group(id);
    if (!group_opt) {
        // error to be handled: schedule not found
        return *this;
    }
    auto group_label = *group_opt;
    auto&& group     = m_schedule_groups.at(group_label);
    auto&& schedule  = group.schedules.at(id);
    schedule->remove_set(label);
    return *this;
};
EPIX_API App& App::remove_set(const SystemSetLabel& label) {
    std::shared_lock lock(*m_mutex);
    for (auto&& [schedules_label, group] : m_schedule_groups) {
        for (auto&& [schedule_label, schedule] : group.schedules) {
            schedule->remove_set(label);
        }
    }
    return *this;
};

EPIX_API App& App::add_resource(const UntypedRes& resource) {
    if (auto* w = get_world(MainWorld)) {
        w->add_resource(resource);
    }
    return *this;
}

EPIX_API void App::build() {
    // build plugins
    for (size_t i = 0; i < m_plugins.size(); ++i) {
        auto plugin = m_plugins[i].second;
        if (plugin) {
            plugin->build(*this);
        }
    }
    std::shared_lock lock(*m_mutex);
    for (auto&& [label, plugin] : m_plugins) {
        UntypedRes res = UntypedRes::create(
            label, plugin, std::make_shared<std::shared_mutex>()
        );
        for (auto&& [wl, world] : m_worlds) {
            world->add_resource(res);
        }
    }

    // retry to add queued systems
    for (auto&& [label, config] : m_queued_systems) {
        add_systems(label, std::move(config));
    }
    // retry to add queued sets
    for (auto&& [label, config] : m_queued_sets) {
        configure_sets(label, config);
    }

    // build groups
    for (auto&& [label, group] : m_schedule_groups) {
        group.build();
    }
}

EPIX_API void App::set_runner(std::shared_ptr<AppRunner> runner) {
    std::unique_lock lock(*m_mutex);
    m_runner = std::move(runner);
}
EPIX_API int App::run() {
    logger()->info("Building app.");
    build();
    logger()->info("Running app.");
    if (!m_runner) {
        m_runner = std::make_unique<OnceRunner>();
    }
    return m_runner->run(*this);
}
EPIX_API int App::run_group(const GroupLabel& label) {
    if (auto group = get_schedule_group(label)) {
        group->build();
        auto res = group->run(*this);
        if (!res.has_value()) return -1;
        return 0;
    } else {
        return -2;
    }
}

EPIX_API App::TracySettings& App::tracy_settings() { return m_tracy_settings; };
EPIX_API const App::TracySettings& App::tracy_settings() const {
    return m_tracy_settings;
};

EPIX_API void App::run_schedule(const ScheduleLabel& label) {
    if (auto group_opt = find_belonged_group(label)) {
        auto group_label = *group_opt;
        auto&& group     = m_schedule_groups.at(group_label);
        auto&& schedule  = group.schedules.at(label);
        auto src_world   = group.schedule_src.at(label);
        auto dst_world   = group.schedule_dst.at(label);
        auto src         = get_world(src_world);
        auto dst         = get_world(dst_world);
        if (src && dst) {
            ScheduleRunner runner(*schedule, group.schedule_run_once.at(label));
            runner.set_worlds(*src, *dst);
            runner.set_executors(m_executors);
            auto result = runner.run();
        }
    }
}

EPIX_API App::TracySettings& App::TracySettings::schedule_enable_tracy(
    const ScheduleLabel& label, bool enable
) {
    schedules_enable_tracy[label] = enable;
    return *this;
};
EPIX_API bool App::TracySettings::schedule_enabled_tracy(
    const ScheduleLabel& label
) {
    if (auto it = schedules_enable_tracy.find(label);
        it != schedules_enable_tracy.end()) {
        return it->second;
    } else {
        schedules_enable_tracy[label] = enable_tracy;
        return enable_tracy;
    }
};
#include "epix/app.h"

using namespace epix::app;

EPIX_API UntypedRes World::resource(std::type_index type) {
    auto&& it = m_resources.find(type);
    if (it == m_resources.end()) return UntypedRes{};
    return it->second;
}
EPIX_API void World::add_resource(std::type_index type, UntypedRes res) {
    std::unique_lock lock(m_resources_mutex);
    if (!m_resources.contains(type)) {
        m_resources.emplace(type, res);
    }
}

EPIX_API void Executor::add(const std::string& name, size_t count) {
    m_pools.emplace(
        name, std::make_shared<BS::thread_pool<BS::tp::priority>>(
                  count, [=]() { BS::this_thread::set_os_thread_name(name); }
              )
    );
}
EPIX_API std::shared_ptr<BS::thread_pool<BS::tp::priority>> Executor::get(
    const std::string& name
) {
    auto it = m_pools.find(name);
    if (it == m_pools.end()) return nullptr;
    return it->second;
}

EPIX_API App App::create() { return App(); }
EPIX_API App App::create2() { return App(); }

EPIX_API App::App() {
    m_pool     = std::make_unique<BS::thread_pool<BS::tp::priority>>(2, []() {
        // set thread name to "control"
        BS::this_thread::set_os_thread_name("control");
    });
    m_executor = std::make_shared<Executor>();
    m_executor->add("default", 4);
    m_executor->add("single", 1);
    m_enable_loop = std::make_unique<bool>(false);
    add_world<MainWorld>();
    add_world<RenderWorld>();
    add_startup_schedule(Schedule(PreStartup)
                             .set_src_world<MainWorld>()
                             .set_dst_world<MainWorld>());
    add_startup_schedule(Schedule(Startup)
                             .set_src_world<MainWorld>()
                             .set_dst_world<MainWorld>()
                             .after(PreStartup));
    add_startup_schedule(Schedule(PostStartup)
                             .set_src_world<MainWorld>()
                             .set_dst_world<MainWorld>()
                             .after(Startup));
    add_loop_schedule(Schedule(PreExtract)
                          .set_src_world<MainWorld>()
                          .set_dst_world<RenderWorld>());
    add_loop_schedule(Schedule(Extraction)
                          .set_src_world<MainWorld>()
                          .set_dst_world<RenderWorld>()
                          .after(PreExtract));
    add_loop_schedule(Schedule(PostExtract)
                          .set_src_world<MainWorld>()
                          .set_dst_world<RenderWorld>()
                          .after(Extraction));
    add_loop_schedule(Schedule(First)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(PostExtract));
    add_loop_schedule(Schedule(PreUpdate)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(First));
    add_loop_schedule(Schedule(Update)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(PreUpdate));
    add_loop_schedule(Schedule(PostUpdate)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(Update));
    add_loop_schedule(Schedule(Last)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(PostUpdate));
    add_loop_schedule(Schedule(Prepare)
                          .set_src_world<RenderWorld>()
                          .set_dst_world<RenderWorld>()
                          .after(PostExtract));
    add_loop_schedule(Schedule(PreRender)
                          .set_src_world<RenderWorld>()
                          .set_dst_world<RenderWorld>()
                          .after(Prepare));
    add_loop_schedule(Schedule(Render)
                          .set_src_world<RenderWorld>()
                          .set_dst_world<RenderWorld>()
                          .after(PreRender));
    add_loop_schedule(Schedule(PostRender)
                          .set_src_world<RenderWorld>()
                          .set_dst_world<RenderWorld>()
                          .after(Render));
    add_exit_schedule(
        Schedule(PreExit).set_src_world<MainWorld>().set_dst_world<MainWorld>()
    );
    add_exit_schedule(Schedule(Exit)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(PreExit));
    add_exit_schedule(Schedule(PostExit)
                          .set_src_world<MainWorld>()
                          .set_dst_world<MainWorld>()
                          .after(Exit));
    add_event<AppExit>();
}

EPIX_API App* App::operator->() { return this; }
EPIX_API App& App::enable_loop() {
    *m_enable_loop = true;
    return *this;
};

EPIX_API App& App::set_log_level(spdlog::level::level_enum level) {
    return *this;
};

EPIX_API App& App::add_startup_schedule(Schedule&& schedule) {
    schedule.set_executor(m_executor);
    auto id            = schedule.m_id;
    auto&& m_schedules = startup_graph.m_schedules;
    if (m_schedules.contains(id)) {
        m_schedules[id] = std::make_shared<Schedule>(std::move(schedule));
    } else {
        m_schedules.emplace(
            id, std::make_shared<Schedule>(std::move(schedule))
        );
    }
    return *this;
};
EPIX_API App& App::add_startup_schedule(Schedule& schedule) {
    add_startup_schedule(std::move(schedule));
    return *this;
};
EPIX_API App& App::add_loop_schedule(Schedule&& schedule) {
    schedule.set_executor(m_executor);
    auto id            = schedule.m_id;
    auto&& m_schedules = loop_graph.m_schedules;
    if (m_schedules.contains(id)) {
        m_schedules[id] = std::make_shared<Schedule>(std::move(schedule));
    } else {
        m_schedules.emplace(
            id, std::make_shared<Schedule>(std::move(schedule))
        );
    }
    return *this;
};
EPIX_API App& App::add_loop_schedule(Schedule& schedule) {
    add_loop_schedule(std::move(schedule));
    return *this;
};
EPIX_API App& App::add_exit_schedule(Schedule&& schedule) {
    schedule.set_executor(m_executor);
    auto id            = schedule.m_id;
    auto&& m_schedules = exit_graph.m_schedules;
    if (m_schedules.contains(id)) {
        m_schedules[id] = std::make_shared<Schedule>(std::move(schedule));
    } else {
        m_schedules.emplace(
            id, std::make_shared<Schedule>(std::move(schedule))
        );
    }
    return *this;
};
EPIX_API App& App::add_exit_schedule(Schedule& schedule) {
    add_exit_schedule(std::move(schedule));
    return *this;
};

EPIX_API void App::build_plugins() {
    for (size_t i = 0; i < m_plugins.size(); i++) {
        auto&& [id, plugin] = m_plugins[i];
        plugin->build(*this);
        auto mutex = std::make_shared<std::shared_mutex>();
        for (auto&& [type, world] : m_worlds) {
            if (!world->resource(id).resource) {
                world->add_resource(
                    id,
                    UntypedRes{std::static_pointer_cast<void>(plugin), mutex}
                );
            }
        }
    }
};
EPIX_API void App::build(ScheduleGraph& graph) {
    auto&& m_schedules = graph.m_schedules;
    for (auto&& [id, schedule] : m_schedules) {
        schedule->build();
        schedule->m_prev_schedules.clear();
        schedule->m_next_schedules.clear();
    }
    for (auto&& [id, schedule] : m_schedules) {
        for (auto&& each : schedule->m_prev_ids) {
            if (auto it = m_schedules.find(each); it != m_schedules.end()) {
                schedule->m_prev_schedules.emplace(it->second);
                it->second->m_next_schedules.emplace(schedule);
            }
        }
        for (auto&& each : schedule->m_next_ids) {
            if (auto it = m_schedules.find(each); it != m_schedules.end()) {
                schedule->m_next_schedules.emplace(it->second);
                it->second->m_prev_schedules.emplace(schedule);
            }
        }
    }
};
EPIX_API void App::bake(ScheduleGraph& graph) {
    auto&& m_schedules = graph.m_schedules;
    std::vector<std::shared_ptr<Schedule>> schedules;
    schedules.reserve(m_schedules.size());
    for (auto&& [id, schedule] : m_schedules) {
        schedule->clear_tmp();
        schedules.emplace_back(schedule);
    }
    std::sort(
        schedules.begin(), schedules.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs->reach_time() < rhs->reach_time();
        }
    );
    for (size_t i = 0; i < schedules.size(); i++) {
        for (size_t j = i + 1; j < schedules.size(); j++) {
            if (schedules[i]->m_src_world == schedules[j]->m_dst_world ||
                schedules[i]->m_dst_world == schedules[j]->m_src_world ||
                schedules[i]->m_src_world == schedules[j]->m_src_world ||
                schedules[i]->m_dst_world == schedules[j]->m_dst_world) {
                schedules[i]->m_tmp_nexts.emplace(schedules[j]);
                schedules[j]->m_tmp_prevs.emplace(schedules[i]);
            }
        }
    }
};

EPIX_API void App::run(ScheduleGraph& graph) {
    bake(graph);
    auto&& m_schedules = graph.m_schedules;
    auto&& m_finishes  = graph.m_finishes;
    size_t m_running   = 0;
    size_t m_remain    = m_schedules.size();
    auto run_schedule  = [&](std::shared_ptr<Schedule> schedule) {
        auto src = m_worlds[schedule->m_src_world].get();
        auto dst = m_worlds[schedule->m_dst_world].get();
        if (!src || !dst) {
            m_finishes.emplace(schedule);
            return;
        }
        schedule->bake();
        if (!m_pool) {
            schedule->run(src, dst);
            m_finishes.emplace(schedule);
            return;
        }
        m_pool->detach_task(
            [this, src, dst, schedule, &m_finishes]() mutable {
                schedule->run(src, dst);
                m_finishes.emplace(schedule);
            },
            127
        );
    };
    for (auto&& [id, schedule] : m_schedules) {
        schedule->m_prev_count =
            schedule->m_tmp_prevs.size() + schedule->m_prev_schedules.size();
        if (schedule->m_prev_count == 0) {
            m_running++;
            run_schedule(schedule);
        }
    }
    while (m_running > 0) {
        auto&& schedule = m_finishes.pop();
        m_running--;
        for (auto&& each : schedule->m_tmp_nexts) {
            if (auto ptr = each.lock()) {
                ptr->m_prev_count--;
                if (ptr->m_prev_count == 0) {
                    m_running++;
                    run_schedule(ptr);
                }
            } else {
                schedule->m_tmp_nexts.erase(each);
            }
        }
        for (auto&& each : schedule->m_next_schedules) {
            if (auto ptr = each.lock()) {
                ptr->m_prev_count--;
                if (ptr->m_prev_count == 0) {
                    m_running++;
                    run_schedule(ptr);
                }
            } else {
                schedule->m_tmp_prevs.erase(each);
            }
        }
    }
}

EPIX_API void App::run() {
    build_plugins();
    build(startup_graph);
    build(loop_graph);
    build(exit_graph);
    run(startup_graph);
    std::unique_ptr<BasicSystem<bool>> to_loop =
        std::make_unique<BasicSystem<bool>>([](EventReader<AppExit> read) {
            return read.empty();
        });
    do {
        run(loop_graph);
    } while (*m_enable_loop && to_loop->run(
                                   m_worlds[typeid(MainWorld)].get(),
                                   m_worlds[typeid(MainWorld)].get()
                               ));
    run(exit_graph);
}
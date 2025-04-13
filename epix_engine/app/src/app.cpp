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

EPIX_API App App::create(const AppCreateInfo& info) { return App(info); }
EPIX_API App App::create2(const AppCreateInfo& info) { return App(info); }

EPIX_API App::App(const AppCreateInfo& info) {
    if (info.control_threads)
        m_pool = std::make_unique<BS::thread_pool<BS::tp::priority>>(
            info.control_threads,
            []() {
                // set thread name to "control"
                BS::this_thread::set_os_thread_name("control");
            }
        );
    m_executor = std::make_shared<Executor>();
    for (auto&& [name, count] : info.worker_threads) {
        m_executor->add(name, count);
    }
    m_enable_loop      = std::make_unique<bool>(info.enable_loop);
    m_enable_tracy     = std::make_unique<bool>(info.enable_tracy);
    m_tracy_frame_mark = std::make_unique<bool>(info.enable_frame_mark);
    set_logger(info.logger ? info.logger : spdlog::default_logger());
    m_graphs.emplace(typeid(StartGraphT), std::make_unique<ScheduleGraph>());
    m_graphs.emplace(typeid(LoopGraphT), std::make_unique<ScheduleGraph>());
    m_graphs.emplace(typeid(ExitGraphT), std::make_unique<ScheduleGraph>());
    add_world<MainWorld>();
    add_world<RenderWorld>();
    add_schedule(
        StartGraph,
        Schedule(PreStartup)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>(),
        Schedule(Startup)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(PreStartup),
        Schedule(PostStartup)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(Startup)
    );
    add_schedule(
        LoopGraph,
        Schedule(PreExtract)
            .set_src_world<MainWorld>()
            .set_dst_world<RenderWorld>(),
        Schedule(Extraction)
            .set_src_world<MainWorld>()
            .set_dst_world<RenderWorld>()
            .after(PreExtract),
        Schedule(PostExtract)
            .set_src_world<MainWorld>()
            .set_dst_world<RenderWorld>()
            .after(Extraction),
        Schedule(First)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(PostExtract),
        Schedule(PreUpdate)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(First),
        Schedule(Update)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(PreUpdate),
        Schedule(PostUpdate)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(Update),
        Schedule(Last)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(PostUpdate),
        Schedule(Prepare)
            .set_src_world<RenderWorld>()
            .set_dst_world<RenderWorld>()
            .after(PostExtract),
        Schedule(PreRender)
            .set_src_world<RenderWorld>()
            .set_dst_world<RenderWorld>()
            .after(Prepare),
        Schedule(Render)
            .set_src_world<RenderWorld>()
            .set_dst_world<RenderWorld>()
            .after(PreRender),
        Schedule(PostRender)
            .set_src_world<RenderWorld>()
            .set_dst_world<RenderWorld>()
            .after(Render)
    );
    add_schedule(
        ExitGraph,
        Schedule(PreExit).set_src_world<MainWorld>().set_dst_world<MainWorld>(),
        Schedule(Exit)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(PreExit),
        Schedule(PostExit)
            .set_src_world<MainWorld>()
            .set_dst_world<MainWorld>()
            .after(Exit)
    );
    add_event<AppExit>();
}

EPIX_API App* App::operator->() { return this; }
EPIX_API App& App::enable_loop() {
    *m_enable_loop = true;
    return *this;
};
EPIX_API App& App::disable_loop() {
    *m_enable_loop = false;
    return *this;
};
EPIX_API App& App::enable_tracy() {
    *m_enable_tracy = true;
    return *this;
};
EPIX_API App& App::disable_tracy() {
    *m_enable_tracy = false;
    return *this;
};
EPIX_API App& App::enable_frame_mark() {
    *m_tracy_frame_mark = true;
    return *this;
};
EPIX_API App& App::disable_frame_mark() {
    *m_tracy_frame_mark = false;
    return *this;
};

EPIX_API App& App::set_log_level(spdlog::level::level_enum level) {
    return *this;
};

EPIX_API App& App::add_schedule(GraphId gid, Schedule&& schedule) {
    schedule.set_executor(m_executor);
    schedule.set_logger(m_logger);
    auto id = schedule.m_id;
    if (!m_graphs.contains(gid) || m_graph_ids.contains(id)) {
        return *this;
    }
    m_graph_ids.emplace(id, gid);
    auto&& m_schedules = m_graphs.at(gid)->m_schedules;
    m_schedules.emplace(id, std::make_shared<Schedule>(std::move(schedule)));
    return *this;
};
EPIX_API App& App::add_schedule(GraphId id, Schedule& schedule) {
    add_schedule(id, std::move(schedule));
    return *this;
};

EPIX_API App& App::set_logger(const std::shared_ptr<spdlog::logger>& logger) {
    m_logger = logger->clone("app");
    for (auto&& [id, graph] : m_graphs) {
        for (auto&& [type, schedule] : graph->m_schedules) {
            schedule->set_logger(m_logger);
        }
    }
    return *this;
};
EPIX_API std::shared_ptr<spdlog::logger> App::logger() const {
    return m_logger;
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
        if (*m_enable_tracy) {
            ZoneScopedN("Try Detach Schedule");
            auto name = std::format(
                "Detach Schedule: {}#{}", schedule->m_id.type.name(),
                schedule->m_id.value
            );
            ZoneName(name.c_str(), name.size());
            auto src = m_worlds[schedule->m_src_world].get();
            auto dst = m_worlds[schedule->m_dst_world].get();
            if (!src || !dst) {
                m_finishes.emplace(schedule);
                return;
            }
            if (!m_pool) {
                schedule->run(src, dst, true);
                m_finishes.emplace(schedule);
                return;
            }
            m_pool->detach_task(
                [this, src, dst, schedule, &m_finishes]() mutable {
                    schedule->run(src, dst, true);
                    m_finishes.emplace(schedule);
                },
                127
            );
        } else {
            auto src = m_worlds[schedule->m_src_world].get();
            auto dst = m_worlds[schedule->m_dst_world].get();
            if (!src || !dst) {
                m_finishes.emplace(schedule);
                return;
            }
            if (!m_pool) {
                schedule->run(src, dst, false);
                m_finishes.emplace(schedule);
                return;
            }
            m_pool->detach_task(
                [this, src, dst, schedule, &m_finishes]() mutable {
                    schedule->run(src, dst, false);
                    m_finishes.emplace(schedule);
                },
                127
            );
        }
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
    m_logger->info("Building App");
    build_plugins();
    auto&& startup_graph = *m_graphs.at(typeid(StartGraphT));
    auto&& loop_graph    = *m_graphs.at(typeid(LoopGraphT));
    auto&& exit_graph    = *m_graphs.at(typeid(ExitGraphT));
    build(startup_graph);
    build(loop_graph);
    build(exit_graph);
    const bool enable_loop  = *m_enable_loop;
    const bool enable_tracy = *m_enable_tracy;
    const bool frame_mark   = *m_tracy_frame_mark;
    std::unique_ptr<BasicSystem<void>> update_profile =
        std::make_unique<BasicSystem<void>>(
            [&](ResMut<AppProfile> profile,
                Local<std::optional<std::chrono::steady_clock::time_point>>
                    last_time) {
                if (!last_time->has_value()) {
                    *last_time = std::chrono::high_resolution_clock::now();
                    return;
                }
                auto now = std::chrono::high_resolution_clock::now();
                auto delta =
                    (double)
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            now - last_time->value()
                        )
                            .count() /
                    1000000.0;
                float factor = 0.1;
                profile->frame_time =
                    profile->frame_time * (1 - factor) + delta * factor;
                profile->fps = 1000.0 / profile->frame_time;
                *last_time   = now;
            }
        );
    std::unique_ptr<BasicSystem<void>> sync_info =
        std::make_unique<BasicSystem<void>>([&](Res<AppInfo> info) {
            *m_enable_loop      = info->enable_loop;
            *m_enable_tracy     = info->enable_tracy;
            *m_tracy_frame_mark = info->tracy_frame_mark;
        });
    // add AppProfile to MainWorld
    auto&& w = world<MainWorld>();
    w.init_resource<AppProfile>();
    w.insert_resource(AppInfo{
        .enable_loop      = *m_enable_loop,
        .enable_tracy     = *m_enable_tracy,
        .tracy_frame_mark = *m_tracy_frame_mark,
    });
    m_logger->info("Running App");
    m_logger->debug("Running startup schedules");
    run(startup_graph);
    std::unique_ptr<BasicSystem<bool>> to_loop =
        std::make_unique<BasicSystem<bool>>([&](EventReader<AppExit> read) {
            return *m_enable_loop && read.empty();
        });
    m_logger->debug("Running loop schedules");
    do {
        if (*m_tracy_frame_mark) {
            FrameMark;
        }
        // update profile
        auto&& w2 = get_world<MainWorld>();
        update_profile->run(w2, w2);
        run(loop_graph);
    } while (to_loop->run(
        m_worlds[typeid(MainWorld)].get(), m_worlds[typeid(MainWorld)].get()
    ));
    m_logger->info("Exiting App");
    m_logger->debug("Running exit schedules");
    run(exit_graph);
    // remove AppProfile from MainWorld
    auto&& w2 = world<MainWorld>();
    w2.remove_resource<AppProfile>();
    w2.remove_resource<AppInfo>();
    m_logger->info("App terminated.");
    *m_enable_loop      = enable_loop;
    *m_enable_tracy     = enable_tracy;
    *m_tracy_frame_mark = frame_mark;
}
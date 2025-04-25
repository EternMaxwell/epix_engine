#include "epix/app.h"

using namespace epix::app;

EPIX_API Schedule::Schedule(ScheduleId id)
    : m_id(id),
      m_src_world(typeid(void)),
      m_dst_world(typeid(void)),
      m_run_once(false),
      m_finishes(std::make_shared<
                 epix::utils::async::ConQueue<std::shared_ptr<System>>>()) {
    m_logger   = spdlog::default_logger()->clone(id.name());
    m_op_mutex = std::make_unique<std::mutex>();
}

EPIX_API Schedule& Schedule::set_executor(
    const std::shared_ptr<Executor>& executor
) {
    m_executor = executor;
    return *this;
}

EPIX_API Schedule& Schedule::set_logger(
    const std::shared_ptr<spdlog::logger>& logger
) {
    m_logger = logger->clone(m_id.name());
    return *this;
}

EPIX_API Schedule& Schedule::add_system(SystemAddInfo&& info) {
    if (m_running) {
        std::unique_lock lock(*m_op_mutex);
        m_cached_ops.emplace_back(1, static_cast<uint32_t>(m_adds.size()));
        m_adds.emplace_back(std::move(info));
        return *this;
    }
    for (size_t i = 0; i < info.m_systems.size(); i++) {
        auto&& each = info.m_systems[i];
        auto system = std::make_shared<System>(
            each.name, each.index, std::move(each.system)
        );
        system->sets        = std::move(each.m_in_sets);
        system->m_ptr_prevs = std::move(each.m_ptr_prevs);
        system->m_ptr_nexts = std::move(each.m_ptr_nexts);
        system->worker      = std::move(each.m_worker);
        system->conditions  = std::move(each.conditions);
        m_systems.emplace(each.index, system);
    }
    return *this;
}
EPIX_API Schedule& Schedule::remove_system(FuncIndex index) {
    if (m_running) {
        std::unique_lock lock(*m_op_mutex);
        m_cached_ops.emplace_back(0, static_cast<uint32_t>(m_removes.size()));
        m_removes.emplace_back(index);
        return *this;
    }
    m_systems.erase(index);
    return *this;
}

EPIX_API void Schedule::build() {
    {
        std::unique_lock lock(*m_op_mutex);
        for (auto&& [op, id] : m_cached_ops) {
            switch (op) {
                case 0:
                    m_systems.erase(m_removes[id]);
                    break;
                case 1:
                    for (size_t i = 0; i < m_adds[id].m_systems.size(); i++) {
                        auto&& each = m_adds[id].m_systems[i];
                        auto system = std::make_shared<System>(
                            each.name, each.index, std::move(each.system)
                        );
                        system->sets        = std::move(each.m_in_sets);
                        system->m_ptr_prevs = std::move(each.m_ptr_prevs);
                        system->m_ptr_nexts = std::move(each.m_ptr_nexts);
                        system->worker      = std::move(each.m_worker);
                        system->conditions  = std::move(each.conditions);
                        if (m_systems.contains(each.index)) {
                            m_logger->warn(
                                "System {} already exists, replacing.",
                                system->label
                            );
                        }
                        m_systems.emplace(each.index, system);
                    }
                    break;
                default:
                    break;
            }
        }
        m_removes.clear();
        m_adds.clear();
        m_cached_ops.clear();
    }
    for (auto&& [ptr, system] : m_systems) {
        system->m_prevs.clear();
        system->m_nexts.clear();
    }
    for (auto&& [ptr, system] : m_systems) {
        for (auto&& each : system->m_ptr_prevs) {
            if (auto it = m_systems.find(each); it != m_systems.end()) {
                system->m_prevs.emplace(it->second);
                it->second->m_nexts.emplace(system);
            }
        }
        for (auto&& each : system->m_ptr_nexts) {
            if (auto it = m_systems.find(each); it != m_systems.end()) {
                system->m_nexts.emplace(it->second);
                it->second->m_prevs.emplace(system);
            }
        }
    }
    for (auto&& [type, sets] : m_sets) {
        std::vector<entt::dense_set<std::shared_ptr<System>>> tsets;
        tsets.reserve(sets.size());
        for (auto&& set : sets) {
            auto& systems_in_set = tsets.emplace_back();
            for (auto&& [ptr, system] : m_systems) {
                if (std::find_if(
                        system->sets.begin(), system->sets.end(),
                        [&set](const auto& s) { return s == set; }
                    ) != system->sets.end()) {
                    systems_in_set.emplace(system);
                }
            }
        }
        for (size_t i = 0; i < tsets.size(); i++) {
            for (size_t j = i + 1; j < tsets.size(); j++) {
                for (auto& ptri : tsets[i]) {
                    for (auto& ptrj : tsets[j]) {
                        if (ptri->system->contrary_to(ptrj->system.get())) {
                            ptri->m_nexts.emplace(ptrj);
                            ptrj->m_prevs.emplace(ptri);
                        }
                    }
                }
            }
        }
    }
}
EPIX_API void Schedule::bake() {
    for (auto&& [ptr, system] : m_systems) {
        system->clear_tmp();
    }
    std::vector<std::shared_ptr<System>> systems;
    systems.reserve(m_systems.size());
    for (auto&& [ptr, system] : m_systems) {
        systems.emplace_back(system);
    }
    std::sort(
        systems.begin(), systems.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs->reach_time() < rhs->reach_time();
        }
    );
    for (size_t i = 0; i < systems.size(); i++) {
        for (size_t j = i + 1; j < systems.size(); j++) {
            if (systems[i]->system->contrary_to(systems[j]->system.get())) {
                systems[i]->m_tmp_nexts.emplace(systems[j]);
                systems[j]->m_tmp_prevs.emplace(systems[i]);
            }
        }
    }
}
EPIX_API void Schedule::run(World* src, World* dst, bool enable_tracy) {
    if (!m_cached_ops.empty()) {
        m_logger->debug("Rebuilding Schedule.");
        build();
    }
    if (m_systems.empty()) {
        m_avg_time *= 0.9;
        m_last_time = 0.0;
        return;
    }
    m_running       = true;
    auto start      = std::chrono::high_resolution_clock::now();
    size_t m_remain = m_systems.size();
    size_t running  = 0;
    if (enable_tracy) {
        ZoneScopedN("Run Schedule");
        auto name = std::format("Run Schedule {}", m_id.name());
        ZoneName(name.c_str(), name.size());
        {
            ZoneScopedN("Baking Schedule");
            bake();
        }
        for (auto&& [ptr, system] : m_systems) {
            system->m_prev_count =
                system->m_tmp_prevs.size() + system->m_prevs.size();
            system->m_next_count =
                system->m_tmp_nexts.size() + system->m_nexts.size();
            if (system->m_prev_count == 0) {
                running++;
                run(system, src, dst, enable_tracy);
            }
        }
        while (running > 0) {
            auto&& system = m_finishes->pop();
            running--;
            m_remain--;
            for (auto&& each : system->m_nexts) {
                if (auto ptr = each.lock()) {
                    ptr->m_prev_count--;
                    if (ptr->m_prev_count == 0) {
                        running++;
                        run(ptr, src, dst, enable_tracy);
                    }
                } else {
                    system->m_tmp_prevs.erase(each);
                }
            }
            for (auto&& each : system->m_tmp_nexts) {
                if (auto ptr = each.lock()) {
                    ptr->m_prev_count--;
                    if (ptr->m_prev_count == 0) {
                        running++;
                        run(ptr, src, dst, enable_tracy);
                    }
                } else {
                    system->m_tmp_nexts.erase(each);
                }
            }
        }
        dst->m_command_queue.flush(*dst);
    } else {
        bake();
        for (auto&& [ptr, system] : m_systems) {
            system->m_prev_count =
                system->m_tmp_prevs.size() + system->m_prevs.size();
            system->m_next_count =
                system->m_tmp_nexts.size() + system->m_nexts.size();
            if (system->m_prev_count == 0) {
                running++;
                run(system, src, dst, enable_tracy);
            }
        }
        while (running > 0) {
            auto&& system = m_finishes->pop();
            running--;
            m_remain--;
            for (auto&& each : system->m_nexts) {
                if (auto ptr = each.lock()) {
                    ptr->m_prev_count--;
                    if (ptr->m_prev_count == 0) {
                        running++;
                        run(ptr, src, dst, enable_tracy);
                    }
                } else {
                    system->m_tmp_prevs.erase(each);
                }
            }
            for (auto&& each : system->m_tmp_nexts) {
                if (auto ptr = each.lock()) {
                    ptr->m_prev_count--;
                    if (ptr->m_prev_count == 0) {
                        running++;
                        run(ptr, src, dst, enable_tracy);
                    }
                } else {
                    system->m_tmp_nexts.erase(each);
                }
            }
        }
        dst->m_command_queue.flush(*dst);
    }
    if (m_remain != 0) {
        m_logger->warn("Some systems are not finished.");
    }
    if (m_run_once) {
        m_systems.clear();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto delta =
        (double
        )std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count() /
        1000000.0;
    m_avg_time  = delta * 0.1 + m_avg_time * 0.9;
    m_last_time = delta;
    m_running   = false;
}
EPIX_API void Schedule::run(
    std::shared_ptr<System> system, World* src, World* dst, bool enable_tracy
) {
    if (enable_tracy) {
        ZoneScopedN("Detach System");
        auto name = std::format("Detach System: {}", system->label);
        ZoneName(name.c_str(), name.size());
        auto&& pool = m_executor->get(system->worker);
        if (!pool) {
            system->run(src, dst, true);
            m_finishes->emplace(system);
        }
        pool->detach_task(
            [this, src, dst, system]() mutable {
                system->run(src, dst, true);
                m_finishes->emplace(system);
            },
            127
        );
    } else {
        auto&& pool = m_executor->get(system->worker);
        if (!pool) {
            system->run(src, dst, false);
            m_finishes->emplace(system);
        }
        pool->detach_task(
            [this, src, dst, system]() mutable {
                system->run(src, dst, false);
                m_finishes->emplace(system);
            },
            127
        );
    }
}
EPIX_API Schedule& Schedule::run_once(bool once) {
    m_run_once = once;
    return *this;
}
EPIX_API double Schedule::get_avg_time() const { return m_avg_time; }
EPIX_API void Schedule::clear_tmp() {
    m_reach_time.reset();
    m_tmp_prevs.clear();
    m_tmp_nexts.clear();
}
EPIX_API double Schedule::reach_time() {
    if (m_reach_time.has_value()) return m_reach_time.value();
    m_reach_time = 0.0;
    for (auto&& each : m_prev_schedules) {
        if (auto ptr = each.lock()) {
            m_reach_time = std::max(
                m_reach_time.value(), ptr->reach_time() + ptr->get_avg_time()
            );
        } else {
            m_prev_schedules.erase(each);
        }
    }
    return m_avg_time;
}

EPIX_API ScheduleProfiles::ScheduleProfile& ScheduleProfiles::profile(
    ScheduleId id
) {
    if (auto it = m_profiles.find(id); it != m_profiles.end()) {
        return it->second;
    } else {
        m_profiles.emplace(id, ScheduleProfile{});
        return m_profiles.at(id);
    }
}

EPIX_API const ScheduleProfiles::ScheduleProfile& ScheduleProfiles::profile(
    ScheduleId id
) const {
    if (auto it = m_profiles.find(id); it != m_profiles.end()) {
        return it->second;
    } else {
        throw std::runtime_error(std::format("Schedule {} not found", id.name())
        );
    }
}

EPIX_API ScheduleProfiles::ScheduleProfile* ScheduleProfiles::get_profile(
    ScheduleId id
) {
    if (auto it = m_profiles.find(id); it != m_profiles.end()) {
        return &it->second;
    } else {
        m_profiles.emplace(id, ScheduleProfile{});
        return &m_profiles.at(id);
    }
}

EPIX_API const ScheduleProfiles::ScheduleProfile* ScheduleProfiles::get_profile(
    ScheduleId id
) const {
    if (auto it = m_profiles.find(id); it != m_profiles.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

EPIX_API void ScheduleProfiles::for_each(
    const std::function<void(ScheduleId, ScheduleProfile&)>& func
) {
    for (auto&& [id, profile] : m_profiles) {
        func(id, profile);
    }
}

EPIX_API void ScheduleProfiles::for_each(
    const std::function<void(ScheduleId, const ScheduleProfile&)>& func
) const {
    for (auto&& [id, profile] : m_profiles) {
        func(id, profile);
    }
}
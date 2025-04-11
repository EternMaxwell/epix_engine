#include "epix/app.h"

using namespace epix::app;

EPIX_API bool ScheduleId::operator==(const ScheduleId& other) const {
    return type == other.type && value == other.value;
}

EPIX_API Schedule::Schedule(ScheduleId id)
    : m_id(id),
      m_src_world(typeid(void)),
      m_dst_world(typeid(void)),
      m_finishes(std::make_shared<
                 index::concurrent::conqueue<std::shared_ptr<System>>>()) {
    m_logger = spdlog::default_logger()->clone(
        std::format("{}#{}", id.type.name(), id.value)
    );
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
    m_logger =
        logger->clone(std::format("{}#{}", m_id.type.name(), m_id.value));
    return *this;
}

EPIX_API Schedule& Schedule::add_system(SystemAddInfo&& info) {
    for (size_t i = 0; i < info.m_systems.size(); i++) {
        auto&& each = info.m_systems[i];
        auto system = std::make_shared<System>(
            each.name, each.index, std::move(each.system)
        );
        system->sets        = std::move(each.m_in_sets);
        system->m_ptr_prevs = std::move(each.m_ptr_prevs);
        system->m_ptr_nexts = std::move(each.m_ptr_nexts);
        system->worker      = std::move(each.m_worker);
        std::move(
            each.conditions.begin(), each.conditions.end(),
            std::back_inserter(system->conditions)
        );
        if (info.m_chain) {
            for (size_t j = i + 1; j < info.m_systems.size(); j++) {
                system->m_ptr_nexts.emplace(info.m_systems[j].index);
            }
        }
        m_systems.emplace(each.index, system);
    }
    return *this;
}

EPIX_API void Schedule::build() {
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
EPIX_API void Schedule::run(World* src, World* dst) {
    if (m_systems.empty()) return;
    auto start = std::chrono::high_resolution_clock::now();
    ZoneScopedN("Run Schedule");
    auto name = std::format("Run Schedule {}#{}", m_id.type.name(), m_id.value);
    ZoneName(name.c_str(), name.size());
    {
        ZoneScopedN("Baking Schedule");
        bake();
    }
    size_t m_remain  = m_systems.size();
    size_t m_running = 0;
    for (auto&& [ptr, system] : m_systems) {
        system->m_prev_count =
            system->m_tmp_prevs.size() + system->m_prevs.size();
        system->m_next_count =
            system->m_tmp_nexts.size() + system->m_nexts.size();
        if (system->m_prev_count == 0) {
            m_running++;
            run(system, src, dst);
        }
    }
    while (m_running > 0) {
        auto&& system = m_finishes->pop();
        m_running--;
        m_remain--;
        for (auto&& each : system->m_tmp_nexts) {
            if (auto ptr = each.lock()) {
                ptr->m_prev_count--;
                if (ptr->m_prev_count == 0) {
                    m_running++;
                    run(ptr, src, dst);
                }
            } else {
                system->m_tmp_nexts.erase(each);
            }
        }
        for (auto&& each : system->m_nexts) {
            if (auto ptr = each.lock()) {
                ptr->m_prev_count--;
                if (ptr->m_prev_count == 0) {
                    m_running++;
                    run(ptr, src, dst);
                }
            } else {
                system->m_tmp_prevs.erase(each);
            }
        }
    }
    dst->m_command.flush();
    if (m_remain != 0) {
        m_logger->warn("Some systems are not finished.");
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto delta =
        (double
        )std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count() /
        1000000.0;
    m_avg_time = delta * 0.1 + m_avg_time * 0.9;
}
EPIX_API void Schedule::run(
    std::shared_ptr<System> system, World* src, World* dst
) {
    ZoneScopedN("Detach System");
    auto name   = std::format("Detach System: {}", system->label);
    auto&& pool = m_executor->get(system->worker);
    if (!pool) {
        ZoneScopedN("Run System");
        system->run(src, dst);
        m_finishes->emplace(system);
    }
    pool->detach_task(
        [this, src, dst, system]() mutable {
            system->run(src, dst);
            m_finishes->emplace(system);
        },
        127
    );
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
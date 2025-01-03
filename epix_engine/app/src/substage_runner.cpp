#include <format>
#include <tracy/Tracy.hpp>

#include "epix/app.h"

using namespace epix::app;

EPIX_API void SubStageRunner::build() {
    // clear previous dependencies if any
    // tmp dependencies will be reset in prepare function
    // so no need to clear them here
    for (auto& [ptr, system] : m_systems) {
        system->m_strong_prevs.clear();
        system->m_strong_nexts.clear();
    }
    // user defined dependencies
    for (auto& [ptr, system] : m_systems) {
        for (auto& next_ptr : system->m_ptr_nexts) {
            auto it = m_systems.find(next_ptr);
            if (it != m_systems.end()) {
                system->m_strong_nexts.insert(it->second);
                it->second->m_strong_prevs.insert(system);
            }
        }
        for (auto& prev_ptr : system->m_ptr_prevs) {
            auto it = m_systems.find(prev_ptr);
            if (it != m_systems.end()) {
                system->m_strong_prevs.insert(it->second);
                it->second->m_strong_nexts.insert(system);
            }
        }
    }
    // set dependencies
    for (auto& [type, sets] : *m_sets) {
        std::vector<spp::sparse_hash_set<std::shared_ptr<SystemNode>>> set_ptrs;
        // set_ptrs[i] contains all systems that are in set i
        for (auto& set : sets) {
            auto& per_set_ptrs = set_ptrs.emplace_back();
            for (auto& [ptr, system] : m_systems) {
                if (std::find_if(
                        system->m_in_sets.begin(), system->m_in_sets.end(),
                        [&set](const SystemSet& s) {
                            return s.m_type == set.m_type &&
                                   s.m_value == set.m_value;
                        }
                    ) != system->m_in_sets.end()) {
                    per_set_ptrs.insert(system);
                }
            }
        }
        // add dependencies between systems in different sets
        for (size_t i = 0; i < set_ptrs.size(); ++i) {
            for (size_t j = i + 1; j < set_ptrs.size(); ++j) {
                for (auto& ptr_i : set_ptrs[i]) {
                    for (auto& ptr_j : set_ptrs[j]) {
                        ptr_i->m_strong_nexts.insert(ptr_j);
                        ptr_j->m_strong_prevs.insert(ptr_i);
                    }
                }
            }
        }
    }
}
EPIX_API void SubStageRunner::bake() {
    for (auto& [ptr, system] : m_systems) {
        system->clear_tmp();
    }
    static thread_local std::vector<std::shared_ptr<SystemNode>> systems;
    systems.clear();
    for (auto& [ptr, system] : m_systems) {
        systems.push_back(system);
    }
    std::sort(systems.begin(), systems.end(), [](const auto& a, const auto& b) {
        return a->reach_time() < b->reach_time();
    });
    for (size_t i = 0; i < systems.size(); ++i) {
        for (size_t j = i + 1; j < systems.size(); ++j) {
            if (systems[i]->m_system->contrary_to(systems[j]->m_system.get())) {
                systems[i]->m_weak_nexts.insert(systems[j]);
                systems[j]->m_weak_prevs.insert(systems[i]);
            }
        }
    }
    m_heads.clear();
    for (auto& system : systems) {
        if (system->m_weak_prevs.empty() && system->m_strong_prevs.empty()) {
            m_heads.push_back(system);
        }
    }
}
EPIX_API void SubStageRunner::run(std::shared_ptr<SystemNode> node) {
    auto pool = m_pools->get_pool(node->m_worker);
    if (pool) {
        pool->detach_task([this, node]() {
            auto name =
                std::format("system: {:#018x}", (size_t)node->m_sys_addr.func);
            ZoneTransientN(zone, name.c_str(), true);
            node->run(m_src, m_dst);
            msg_queue.push(node);
        });
    } else {
        m_logger->warn(
            "The runner does not have a worker pool named {}. Skipping "
            "system {:#018x}",
            node->m_worker, (size_t)node->m_sys_addr.func
        );
        msg_queue.push(node);
    }
}
EPIX_API void SubStageRunner::run() {
    for (auto& [ptr, system] : m_systems) {
        system->m_prev_count =
            system->m_strong_prevs.size() + system->m_weak_prevs.size();
        system->m_next_count =
            system->m_strong_nexts.size() + system->m_weak_nexts.size();
    }
    for (auto& head : m_heads) {
        run(head);
    }
    size_t m_remain  = m_systems.size();
    size_t m_running = m_heads.size();
    while (m_running > 0) {
        msg_queue.wait();
        auto ptr = msg_queue.pop();
        --m_remain;
        --m_running;
        for (auto& next_weak : ptr->m_strong_nexts) {
            if (auto next = next_weak.lock()) {
                --next->m_prev_count;
                if (next->m_prev_count == 0) {
                    run(next);
                    ++m_running;
                }
            } else {
                --m_remain;
            }
        }
        for (auto& next_ptr : ptr->m_weak_nexts) {
            if (auto next = next_ptr.lock()) {
                --next->m_prev_count;
                if (next->m_prev_count == 0) {
                    run(next);
                    ++m_running;
                }
            } else {
                --m_remain;
            }
        }
    }
    if (m_remain != 0) {
        m_logger->warn(
            "Stage {} - {} has {} systems not finished. Maybe a deadlock "
            "or some dependencies are removed during runtime.",
            m_sub_stage.m_stage.name(), m_sub_stage.m_sub_stage, m_remain
        );
    }
}
EPIX_API void SubStageRunner::set_log_level(spdlog::level::level_enum level) {
    m_logger->set_level(level);
}
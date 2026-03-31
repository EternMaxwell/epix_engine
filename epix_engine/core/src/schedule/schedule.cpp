module;

#include <spdlog/spdlog.h>

module epix.core;

import std;

import :schedule;
import :labels;

namespace epix::core {
void Schedule::add_config(SetConfig config, bool accept_system) {
    // create node
    if (config.label) {
        auto node = std::make_shared<Node>(*config.label);
        if (accept_system && config.system) node->system = std::move(config.system);
        node->conditions    = std::move(config.conditions);
        node->edges         = std::move(config.edges);
        bool contain_set    = contains_set(*config.label);
        bool contain_system = contains_system(*config.label);
        if (contain_system && accept_system) {
            // fully replace.
            _data.nodes.at(*config.label) = node;
        } else if (contain_set) {
            // merge edges.
            if (contain_system) {
                std::swap(node, _data.nodes.at(*config.label));
            }
            node->edges.merge(_data.nodes.at(*config.label)->edges);
            node->conditions.insert_range(node->conditions.end(),
                                          std::move(_data.nodes.at(*config.label)->conditions) | std::views::as_rvalue);
            _data.nodes.at(*config.label) = node;
        } else {
            _data.nodes.emplace(*config.label, node);
        }
    }
    std::ranges::for_each(config.sub_configs,
                          [&](SetConfig& sub_config) { add_config(std::move(sub_config), accept_system); });
    _data.cache.reset();
}

std::expected<void, SchedulePrepareError> Schedule::prepare(bool check_error) {
    spdlog::trace("[schedule] Preparing schedule '{}', check_error={}.", label().to_string(), check_error);
    // clear validated edges first
    for (auto& [label, pnode] : _data.nodes) {
        pnode->validated_edges.children.clear();
        pnode->validated_edges.parents.clear();
        pnode->validated_edges.depends.clear();
        pnode->validated_edges.successors.clear();
    }
    // complete edges.
    for (auto& [label, pnode] : _data.nodes) {
        auto& node = *pnode;
        // validate edges
        for (const auto& dep_label : node.edges.depends) {
            if (auto it = _data.nodes.find(dep_label); it != _data.nodes.end()) {
                node.validated_edges.depends.insert(dep_label);
                it->second->validated_edges.successors.insert(label);
            } else {
                spdlog::warn(
                    "[app] Schedule [{}]: system set [{}] depends on [{}] which is not in the schedule, ignore this "
                    "dependency.",
                    this->label().to_string(), label.to_string(), dep_label.to_string());
            }
        }
        for (const auto& succ_label : node.edges.successors) {
            if (auto it = _data.nodes.find(succ_label); it != _data.nodes.end()) {
                node.validated_edges.successors.insert(succ_label);
                it->second->validated_edges.depends.insert(label);
            } else {
                spdlog::warn(
                    "[app] Schedule [{}]: system set [{}] is a dependency of [{}] which is not in the schedule, ignore "
                    "this dependency.",
                    this->label().to_string(), label.to_string(), succ_label.to_string());
            }
        }
        for (const auto& parent_label : node.edges.parents) {
            if (auto it = _data.nodes.find(parent_label); it != _data.nodes.end()) {
                node.validated_edges.parents.insert(parent_label);
                it->second->validated_edges.children.insert(label);
            } else {
                spdlog::warn(
                    "[app] Schedule [{}]: system set [{}] is a child of [{}] which is not in the schedule, ignore this "
                    "hierarchy.",
                    this->label().to_string(), label.to_string(), parent_label.to_string());
            }
        }
        for (const auto& child_label : node.edges.children) {
            if (auto it = _data.nodes.find(child_label); it != _data.nodes.end()) {
                node.validated_edges.children.insert(child_label);
                it->second->validated_edges.parents.insert(label);
            } else {
                spdlog::warn(
                    "[app] Schedule [{}]: system set [{}] is a parent of [{}] which is not in the schedule, ignore "
                    "this hierarchy.",
                    this->label().to_string(), label.to_string(), child_label.to_string());
            }
        }
    }
    // rebuild cache
    _data.cache                   = std::make_shared<ScheduleCache>();
    ScheduleCache& schedule_cache = *_data.cache;
    schedule_cache.nodes.reserve(_data.nodes.size());
    for (auto& [label, node] : _data.nodes) {
        schedule_cache.node_map.emplace(label, schedule_cache.nodes.size());
        CachedNode& cached_node = schedule_cache.nodes.emplace_back();
        cached_node.node        = node;
    }
    for (auto&& [index, cached_node] : schedule_cache.nodes | std::views::enumerate) {
        cached_node.depends =
            cached_node.node->validated_edges.depends |
            std::views::transform([&](const SystemSetLabel& label) { return schedule_cache.node_map.at(label); }) |
            std::ranges::to<std::vector<size_t>>();
        cached_node.successors =
            cached_node.node->validated_edges.successors |
            std::views::transform([&](const SystemSetLabel& label) { return schedule_cache.node_map.at(label); }) |
            std::ranges::to<std::vector<size_t>>();
        cached_node.parents =
            cached_node.node->validated_edges.parents |
            std::views::transform([&](const SystemSetLabel& label) { return schedule_cache.node_map.at(label); }) |
            std::ranges::to<std::vector<size_t>>();
        cached_node.children =
            cached_node.node->validated_edges.children |
            std::views::transform([&](const SystemSetLabel& label) { return schedule_cache.node_map.at(label); }) |
            std::ranges::to<std::vector<size_t>>();
    }
    if (!check_error) {
        return {};
    }
    // check if cycles in dependencies
    {
        // time complexity: O(V + E)
        std::vector<size_t> temp_marks(schedule_cache.nodes.size(), 0);
        std::vector<size_t> perm_marks(schedule_cache.nodes.size(), 0);
        std::vector<SystemSetLabel> path;
        std::function<bool(size_t)> visit = [&](size_t index) -> bool {
            if (perm_marks[index]) return false;
            if (temp_marks[index]) {
                path.push_back(schedule_cache.nodes[index].node->label);
                return true;
            }
            temp_marks[index] = 1;
            path.push_back(schedule_cache.nodes[index].node->label);
            for (const auto& succ_index : schedule_cache.nodes[index].successors) {
                if (visit(succ_index)) return true;
            }
            for (const auto& child_index : schedule_cache.nodes[index].children) {
                if (visit(child_index)) return true;
            }
            temp_marks[index] = 0;
            perm_marks[index] = 1;
            path.pop_back();
            return false;
        };
        for (size_t i = 0; i < schedule_cache.nodes.size(); i++) {
            if (!perm_marks[i]) {
                if (visit(i)) {
                    return std::unexpected(SchedulePrepareError{
                        .associated_labels = path,
                        .type              = SchedulePrepareError::Type::CyclicDependency,
                    });
                }
            }
        }
    }
    // check if cycles in hierarchy
    {
        // time complexity: O(V + E)
        std::vector<size_t> temp_marks(schedule_cache.nodes.size(), 0);
        std::vector<size_t> perm_marks(schedule_cache.nodes.size(), 0);
        std::vector<SystemSetLabel> path;
        std::function<bool(size_t)> visit = [&](size_t index) -> bool {
            if (perm_marks[index]) return false;
            if (temp_marks[index]) {
                path.push_back(schedule_cache.nodes[index].node->label);
                return true;
            }
            temp_marks[index] = 1;
            path.push_back(schedule_cache.nodes[index].node->label);
            for (const auto& child_index : schedule_cache.nodes[index].children) {
                if (visit(child_index)) return true;
            }
            temp_marks[index] = 0;
            perm_marks[index] = 1;
            path.pop_back();
            return false;
        };
        for (size_t i = 0; i < schedule_cache.nodes.size(); i++) {
            if (!perm_marks[i]) {
                if (visit(i)) {
                    return std::unexpected(SchedulePrepareError{
                        .associated_labels = path,
                        .type              = SchedulePrepareError::Type::CyclicHierarchy,
                    });
                }
            }
        }
    }
    // check if any nodes have parents with dependencies
    {
        std::vector<bit_vector> reachable_parents(schedule_cache.nodes.size(), bit_vector(schedule_cache.nodes.size()));
        std::vector<bit_vector> reachable_dependencies(schedule_cache.nodes.size(),
                                                       bit_vector(schedule_cache.nodes.size()));
        bit_vector visited(schedule_cache.nodes.size());
        std::function<void(size_t)> dfs_parents = [&](size_t index) {
            if (visited.contains(index)) return;
            visited.set(index);
            reachable_parents[index].set(index);
            for (const auto& parent_index : schedule_cache.nodes[index].parents) {
                dfs_parents(parent_index);
                reachable_parents[index].set(parent_index);
                reachable_parents[index].union_with(reachable_parents[parent_index]);
            }
        };
        std::function<void(size_t)> dfs_dependencies = [&](size_t index) {
            if (visited.contains(index)) return;
            visited.set(index);
            for (const auto& dep_index : schedule_cache.nodes[index].depends) {
                dfs_dependencies(dep_index);
                reachable_dependencies[index].set(dep_index);
                reachable_dependencies[index].union_with(reachable_dependencies[dep_index]);
            }
        };
        visited.clear();
        for (size_t i = 0; i < schedule_cache.nodes.size(); i++) {
            dfs_parents(i);
        }
        visited.clear();
        for (size_t i = 0; i < schedule_cache.nodes.size(); i++) {
            dfs_dependencies(i);
        }
        // for each node, if any pair of its parents has dependencies, report error
        for (size_t i = 0; i < schedule_cache.nodes.size(); i++) {
            auto&& parents = reachable_parents[i].iter_ones();
            std::vector<SystemSetLabel> conflict_parents;
            for (auto&& [p1, p2] : std::views::cartesian_product(parents, parents)) {
                if (p1 >= p2) continue;
                if (reachable_dependencies[p1].contains(p2)) {
                    conflict_parents.push_back(schedule_cache.nodes[p1].node->label);
                    conflict_parents.push_back(schedule_cache.nodes[p2].node->label);
                    break;
                } else if (reachable_dependencies[p2].contains(p1)) {
                    conflict_parents.push_back(schedule_cache.nodes[p2].node->label);
                    conflict_parents.push_back(schedule_cache.nodes[p1].node->label);
                    break;
                }
            }
            if (!conflict_parents.empty()) {
                std::vector<SystemSetLabel> associated_labels;
                associated_labels.push_back(schedule_cache.nodes[i].node->label);
                return std::unexpected(SchedulePrepareError{
                    .associated_labels = associated_labels,
                    .conflict_parents  = std::move(conflict_parents),
                    .type              = SchedulePrepareError::Type::ParentsWithDeps,
                });
            }
        }
    }
    return {};
}

void Schedule::initialize_systems(World& world, bool force) {
    spdlog::trace("[schedule] Initializing systems for schedule '{}', force={}.", label().to_string(), force);
    for (auto& [label, node] : _data.nodes) {
        if (node->system && (!node->system->initialized() || force)) {
            node->system_access = node->system->initialize(world);
        }
        node->condition_access.resize(node->conditions.size());
        for (auto&& [index, condition] :
             node->conditions | std::views::enumerate | std::views::filter([&](auto&& pair) {
                 auto&& [index, condition] = pair;
                 return force || !condition->initialized();
             })) {
            node->condition_access[index] = condition->initialize(world);
        }
    }
    // Initialize pre/post systems
    for (auto& ps : m_pre_systems) {
        if (ps.system && (!ps.initialized || force)) {
            ps.access      = ps.system->initialize(world);
            ps.initialized = true;
        }
    }
    for (auto& ps : m_post_systems) {
        if (ps.system && (!ps.initialized || force)) {
            ps.access      = ps.system->initialize(world);
            ps.initialized = true;
        }
    }
}

void Schedule::check_change_tick(Tick change_tick) {
    // check change tick for all systems
    for (auto& [label, node] : _data.nodes) {
        if (node->system) {
            if (node->system->initialized()) node->system->check_change_tick(change_tick);
        }
        for (auto& condition : node->conditions) {
            if (condition->initialized()) condition->check_change_tick(change_tick);
        }
    }
}

std::unique_ptr<ScheduleExecutor> Schedule::default_executor() {
    return std::make_unique<executors::AutoExecutor>();
}

void Schedule::execute(World& world, const ScheduleConfig& config) {
    spdlog::trace("[schedule] Executing schedule '{}'.", label().to_string());
    if (!executor) {
        set_executor(default_executor());
    }
    if (!_data.cache) {
        auto prepare_result = prepare(
#ifdef NDEBUG
            false
#else
            true
#endif
        );
    }

    try {
        initialize_systems(world);
    } catch (const std::exception& e) {
        spdlog::error("[schedule] exception during system initialization: {}", e.what());
    } catch (...) {
        spdlog::error("[schedule] unknown exception during system initialization.");
    }

    // Check schedule-level conditions (synchronous, caller thread)
    for (auto& cond : config.conditions) {
        if (!cond(world)) return;
    }

    // Run pre-systems (once, before loop body, synchronous)
    for (auto& ps : m_pre_systems) {
        if (ps.system) {
            auto res = ps.system->run({}, world);
        }
    }

    // Main body loop (single execution or repeated via loop_condition)
    do {
        if (config.loop_condition) {
            if (!config.loop_condition(world)) break;
        }

        // Delegate system dispatch to executor
        executor->execute(_data, world, config.executor_config);

        // run_once cleanup: remove executed systems
        if (config.run_once) {
            for (auto it = _data.nodes.begin(); it != _data.nodes.end();) {
                if (it->second->system) {
                    it = _data.nodes.erase(it);
                } else {
                    ++it;
                }
            }
            _data.cache.reset();
        }

        if (!config.loop_condition) break;
    } while (true);

    // Run post-systems (once, after loop body, synchronous)
    for (auto& ps : m_post_systems) {
        if (ps.system) {
            auto res = ps.system->run({}, world);
        }
    }
}

void Schedule::apply_deferred(World& world) {
    if (_data.pending_applies) {
        for (auto&& node : *_data.pending_applies) {
            if (node->system && node->system->is_deferred()) {
                node->system->apply_deferred(world);
            }
        }
        _data.pending_applies.reset();
    }
}

void Schedule::extract_systems_from_config(SetConfig& config, std::vector<PrePostSystem>& target) {
    if (config.system) {
        target.push_back(PrePostSystem{.system = std::move(config.system)});
    }
    for (auto& sub : config.sub_configs) {
        extract_systems_from_config(sub, target);
    }
}

void Schedule::add_pre_systems(SetConfig&& config) { extract_systems_from_config(config, m_pre_systems); }
void Schedule::add_post_systems(SetConfig&& config) { extract_systems_from_config(config, m_post_systems); }

}  // namespace epix::core

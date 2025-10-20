#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <expected>
#include <ranges>

#include "../system/system.hpp"
#include "system_dispatcher.hpp"
#include "system_set.hpp"

namespace epix::core::schedule {
struct Node;
struct Edges {
    std::unordered_set<SystemSetLabel> depends;
    std::unordered_set<SystemSetLabel> successors;
    std::unordered_set<SystemSetLabel> parents;
    std::unordered_set<SystemSetLabel> children;
};
struct Node {
    SystemSetLabel label;
    system::SystemUnique<> system;
    query::FilteredAccessSet system_access;
    std::vector<system::SystemUnique<std::tuple<>, bool>> conditions;
    query::FilteredAccessSet condition_access;

    Edges edges;
    Edges validated_edges;
};
struct CachedNode {
    std::shared_ptr<Node> node;  // stores shared_ptr so you can safely add, remove nodes while executing
    std::vector<size_t> depends;
    std::vector<size_t> successors;
    std::vector<size_t> parents;
    std::vector<size_t> children;
};
struct ScheduleCache {
    std::vector<CachedNode> nodes;
    std::unordered_map<SystemSetLabel, size_t> node_map;
    std::vector<size_t> start_nodes;
};
struct ExecutionState {
    size_t running_count   = 0;
    size_t remaining_count = 0;
    storage::bit_vector ready_nodes;
    storage::bit_vector running_nodes;
    storage::bit_vector finished_nodes;
    storage::bit_vector entered_nodes;
    storage::bit_vector condition_met_nodes;
    std::vector<size_t> wait_count;  // number of dependencies + parents not yet satisfied
    std::vector<size_t> child_count;
};
struct SchedulePrepareError {
    // labels involved in the error, 0 will be the same child if type is ParentsWithDeps
    std::vector<SystemSetLabel> associated_labels;
    // for ParentsWithDeps, the parents the have dependencies
    std::vector<SystemSetLabel> conflict_parents;
    enum class Type {
        CyclicDependency,
        CyclicHierarchy,
        ParentsWithDeps,
    } type;
};
struct Schedule {
    std::unordered_map<SystemSetLabel, std::shared_ptr<Node>> nodes;
    std::optional<ScheduleCache> cache;

    // prepare the schedule (validate and build cache), if check_error is true, will check for errors
    // otherwise the error will cause skipped nodes during execution
    std::expected<void, SchedulePrepareError> prepare(bool check_error = true) {
        // complete edges.
        for (auto& [label, pnode] : nodes) {
            auto& node = *pnode;
            // validate edges
            for (const auto& dep_label : node.edges.depends) {
                if (auto it = nodes.find(dep_label); it != nodes.end()) {
                    node.validated_edges.depends.insert(dep_label);
                    it->second->validated_edges.successors.insert(label);
                }
            }
            for (const auto& succ_label : node.edges.successors) {
                if (auto it = nodes.find(succ_label); it != nodes.end()) {
                    node.validated_edges.successors.insert(succ_label);
                    it->second->validated_edges.depends.insert(label);
                }
            }
            for (const auto& parent_label : node.edges.parents) {
                if (auto it = nodes.find(parent_label); it != nodes.end()) {
                    node.validated_edges.parents.insert(parent_label);
                    it->second->validated_edges.children.insert(label);
                }
            }
            for (const auto& child_label : node.edges.children) {
                if (auto it = nodes.find(child_label); it != nodes.end()) {
                    node.validated_edges.children.insert(child_label);
                    it->second->validated_edges.parents.insert(label);
                }
            }
        }
        // rebuild cache
        cache.emplace();
        ScheduleCache& schedule_cache = *cache;
        schedule_cache.nodes.reserve(nodes.size());
        for (auto& [label, node] : nodes) {
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
            if (cached_node.depends.empty() && cached_node.parents.empty()) {
                schedule_cache.start_nodes.push_back(index);
            }
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
            std::vector<storage::bit_vector> reachable_parents(schedule_cache.nodes.size(),
                                                               storage::bit_vector(schedule_cache.nodes.size()));
            std::vector<storage::bit_vector> reachable_dependencies(schedule_cache.nodes.size(),
                                                                    storage::bit_vector(schedule_cache.nodes.size()));
            storage::bit_vector visited(schedule_cache.nodes.size());
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
                const auto& parents = schedule_cache.nodes[i].parents;
                std::vector<SystemSetLabel> conflict_parents;
                for (auto&& [p1, p2] : std::views::cartesian_product(parents, parents)) {
                    if (p1 >= p2) continue;
                    if (reachable_dependencies[schedule_cache.node_map.at(p1)].contains(
                            schedule_cache.node_map.at(p2))) {
                        conflict_parents.push_back(p1);
                        conflict_parents.push_back(p2);
                        break;
                    }
                }
                if (!conflict_parents.empty()) {
                    std::vector<SystemSetLabel> associated_labels;
                    associated_labels.push_back(schedule_cache.nodes[i].node->label);
                    return std::unexpected(SchedulePrepareError{
                        .associated_labels = associated_labels,
                        .conflict_parents  = conflict_parents,
                        .type              = SchedulePrepareError::Type::ParentsWithDeps,
                    });
                }
            }
        }
        return {};
    }
    void initialize_systems(World& world, bool force = false) {
        for (auto& [label, node] : nodes) {
            if (node->system && (!node->system->initialized() || force)) {
                node->system_access = node->system->initialize(world);
            }
            if (std::ranges::any_of(node->conditions, [](const auto& cond) { return cond && !cond->initialized(); }) ||
                force) {
                node->condition_access = query::FilteredAccessSet{};
                for (auto& condition : node->conditions) {
                    if (condition) node->condition_access.extend(condition->initialize(world));
                }
            }
        }
    }
    void execute(SystemDispatcher& dispatcher) {
        if (!cache) {
            auto prepare_result = prepare(
// compile debug level macro controlled
#ifdef NDEBUG
                false
#else
                true
#endif
            );
            // we still execute even if there are errors, cause it won't crash, just some nodes won't run
        }

        dispatcher.world_scope([this](World& world) { initialize_systems(world); });

        ExecutionState exec_state{
            .running_count       = 0,
            .remaining_count     = cache->nodes.size(),
            .ready_nodes         = storage::bit_vector(cache->nodes.size()),
            .running_nodes       = storage::bit_vector(cache->nodes.size()),
            .finished_nodes      = storage::bit_vector(cache->nodes.size()),
            .entered_nodes       = storage::bit_vector(cache->nodes.size()),
            .condition_met_nodes = storage::bit_vector(cache->nodes.size()),
            .wait_count          = std::vector<size_t>(cache->nodes.size(), 0),
            .child_count         = std::vector<size_t>(cache->nodes.size(), 0),
        };
        // initialize cache value.
        for (auto&& [index, cached_node] : cache->nodes | std::views::enumerate) {
            exec_state.wait_count[index]  = cached_node.depends.size() + cached_node.parents.size();
            exec_state.child_count[index] = cached_node.children.size() + (cached_node.node->system ? 1 : 0);
        }

        // end apply deferred
        dispatcher.apply_deferred(
            exec_state.finished_nodes.iter_ones() |
            std::views::transform([&](size_t index) -> auto& { return *cache->nodes[index].node->system.get(); }));
    }
};
}  // namespace epix::core::schedule
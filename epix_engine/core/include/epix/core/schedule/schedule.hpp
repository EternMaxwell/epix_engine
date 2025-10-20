#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <expected>
#include <numeric>
#include <ostream>
#include <ranges>
#include <utility>

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
    std::vector<query::FilteredAccessSet> condition_access;

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
};
struct ExecutionState {
    size_t running_count   = 0;
    size_t remaining_count = 0;
    storage::bit_vector ready_nodes;
    storage::bit_vector running_nodes;
    storage::bit_vector finished_nodes;
    storage::bit_vector entered_nodes;
    storage::bit_vector condition_met_nodes;
    std::vector<storage::bit_vector> untest_conditions;
    std::vector<size_t> wait_count;  // number of dependencies + parents not yet satisfied
    std::vector<size_t> child_count;
    async_queue finished_queue;
    std::vector<size_t> ready_stack;
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
struct SetConfig {
    SetConfig& after(const SystemSetLabel& label) {
        edges.depends.insert(label);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.after(label); });
        return *this;
    }
    SetConfig& before(const SystemSetLabel& label) {
        edges.successors.insert(label);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.before(label); });
        return *this;
    }
    SetConfig& in_set(const SystemSetLabel& label) {
        edges.parents.insert(label);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.in_set(label); });
        return *this;
    }
    SetConfig& set_name(std::string_view name) {
        if (system) system->set_name(name);
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.set_name(name); });
        return *this;
    }
    template <typename F>
    SetConfig& run_if(F&& func)
        requires(requires { system::make_system<std::tuple<>, bool>(std::forward<F>(func)); })
    {
        conditions.push_back(system::make_system<std::tuple<>, bool>(std::forward<F>(func)));
        std::ranges::for_each(sub_configs, [&](SetConfig& config) { config.run_if(std::forward<F>(func)); });
        return *this;
    }
    SetConfig& chain() {
        for (auto&& [c1, c2] : sub_configs | std::views::adjacent<2>) {
            c1.before(c2.label);
            std::ranges::for_each(c2.sub_configs, [&](SetConfig& config) { c1.before(config.label); });
        }
        return *this;
    }

   private:
    std::optional<SystemSetLabel> label;
    system::SystemUnique<> system;
    std::vector<system::SystemUnique<std::tuple<>, bool>> conditions;
    Edges edges;

    std::vector<SetConfig> sub_configs;

    SetConfig() = default;

    friend struct Schedule;
    template <typename F>
    friend SetConfig single_set(F&& func);
    template <typename... Ts>
    friend SetConfig make_sets(Ts&&... ts);
};
template <bool require_system = false, typename F>
SetConfig single_set(F&& func)
    requires(!require_system || requires { system::make_system<std::tuple<>, void>(std::forward<F>(func)); } ||
             std::same_as<std::decay_t<F>, SetConfig>)
{
    if constexpr (std::same_as<std::decay_t<F>, SetConfig>) {
        return std::forward<F>(func);
    }
    SetConfig config;
    config.label = SystemSetLabel(func);
    if constexpr (requires { system::make_system<std::tuple<>, void>(std::forward<F>(func)); }) {
        config.system = system::make_system<std::tuple<>, void>(std::forward<F>(func));
    }
    return config;
}
template <bool require_system = false, typename... Ts>
SetConfig make_sets(Ts&&... ts) {
    if constexpr (sizeof...(Ts) == 1) {
        return single_set<require_system>(std::get<0>(std::forward_as_tuple(std::forward<Ts>(ts)...)));
    } else {
        SetConfig config;
        config.sub_configs.reserve(sizeof...(Ts));
        (config.sub_configs.push_back(make_sets<require_system>(std::forward<Ts>(ts))), ...);
        return config;
    }
}
struct ExecuteConfig {
    bool apply_direct   = false;  // should call System::apply right after System::run or at the end of the schedule
    bool queue_deferred = false;  // call System::queue for deferred systems instead of apply at the end

    bool is_apply_direct() const { return apply_direct && !queue_deferred; }
    bool is_queue_deferred() const { return queue_deferred; }
    bool is_apply_end() const { return !apply_direct && !queue_deferred; }
};
struct Schedule {
   private:
    std::unordered_map<SystemSetLabel, std::shared_ptr<Node>> nodes;
    std::shared_ptr<ScheduleCache> cache;

    void add_config(SetConfig config, bool accept_system = true) {
        // create node
        if (config.label) {
            auto node   = std::make_shared<Node>();
            node->label = config.label;
            if (accept_system && config.system) node->system = std::move(config.system);
            node->conditions = std::move(config.conditions);
            node->edges      = std::move(config.edges);
            nodes.emplace(node->label, node);
        }
        std::ranges::for_each(config.sub_configs,
                              [&](SetConfig& sub_config) { add_config(std::move(sub_config), accept_system); });
        cache.reset();
    }

   public:
    Schedule() = default;

    void add_systems(SetConfig config) { add_config(std::move(config), true); }
    void configure_sets(SetConfig config) { add_config(std::move(config), false); }

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
        cache                         = std::make_shared<ScheduleCache>();
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
            node->condition_access.resize(node->conditions.size());
            for (auto&& [index, condition] :
                 node->conditions | std::views::enumerate | std::views::filter([&](auto&& pair) {
                     auto&& [index, condition] = pair;
                     return force || !condition->initialized();
                 })) {
                node->condition_access[index] = condition->initialize(world);
            }
        }
    }
    void execute(SystemDispatcher& dispatcher, const ExecuteConfig& config = {}) {
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

        // ? should we initialize systems here? or let caller assure systems are initialized?
        dispatcher.world_scope([this](World& world) { initialize_systems(world); });
        std::shared_ptr cache = this->cache;  // keep a copy to avoid being invalidated during execution

        ExecutionState exec_state{
            .running_count       = 0,
            .remaining_count     = cache->nodes.size(),
            .ready_nodes         = storage::bit_vector(cache->nodes.size()),
            .running_nodes       = storage::bit_vector(cache->nodes.size()),
            .finished_nodes      = storage::bit_vector(cache->nodes.size()),
            .entered_nodes       = storage::bit_vector(cache->nodes.size()),
            .condition_met_nodes = storage::bit_vector(cache->nodes.size(), true),  // default all met
            .untest_conditions   = std::vector<storage::bit_vector>(cache->nodes.size()),
            .wait_count          = std::vector<size_t>(cache->nodes.size(), 0),
            .child_count         = std::vector<size_t>(cache->nodes.size(), 0),
        };
        // initialize cache value.
        for (auto&& [index, cached_node] : cache->nodes | std::views::enumerate) {
            exec_state.wait_count[index]  = cached_node.depends.size() + cached_node.parents.size();
            exec_state.child_count[index] = cached_node.children.size() + (cached_node.node->system ? 1 : 0);
            exec_state.untest_conditions[index].resize(cached_node.node->conditions.size(), true);
            if (exec_state.wait_count[index] == 0) {
                // no dependencies, can run immediately
                exec_state.ready_stack.push_back(index);
            }
        }

        auto dispatch_system = [&](size_t index) {
            // dispatch a system for execution
            CachedNode& cached_node = cache->nodes[index];
            dispatcher.dispatch_system(*cached_node.node->system.get(), {}, cached_node.node->system_access,
                                       {
                                           .on_finish = [&, index]() { exec_state.finished_nodes.set(index); },
                                       });
            if (config.is_apply_direct() && cached_node.node->system->is_deferred()) {
                dispatcher.apply_deferred(*cached_node.node->system.get());
            }
            exec_state.running_count++;
            exec_state.running_nodes.set(index);
        };
        std::vector<size_t> pending_ready;  // ready nodes, but cannot test cause conditions' access conflict with
                                            // running systems in dispatcher
        auto check_cond = [&](size_t index) -> bool {  // return if there are any conditions left untested
            return std::ranges::fold_left(
                exec_state.untest_conditions[index].iter_ones() | std::ranges::to<std::vector>() |  // copied
                    std::views::transform([&](size_t i) {
                        return std::make_tuple(i, std::ref(*cache->nodes[index].node->conditions[i]));
                    }),
                true, [&](bool v, auto&& pair) -> bool {
                    auto&& [cond_index, condition] = pair;
                    auto res_opt                   = dispatcher.try_run_system(condition, {},
                                                                               cache->nodes[index].node->condition_access[cond_index]);
                    if (res_opt.has_value()) {
                        exec_state.condition_met_nodes.set(
                            index, res_opt->value() && exec_state.condition_met_nodes.contains(index));
                        exec_state.untest_conditions[index].reset(cond_index);
                    }
                    return v && res_opt.has_value();
                });
        };
        auto enter_ready = [&]() {
            auto& ready_stack = exec_state.ready_stack;
            std::swap(pending_ready, ready_stack);
            ready_stack.insert_range(ready_stack.end(), pending_ready);
            pending_ready.clear();
            while (!ready_stack.empty()) {
                size_t index = ready_stack.back();
                ready_stack.pop_back();
                exec_state.entered_nodes.set(index);
                CachedNode& cached_node = cache->nodes[index];
                bool cond_met           = exec_state.condition_met_nodes.contains(index);
                if (cond_met) {
                    bool cond_left = check_cond(index);
                    if (cond_left) {
                        // cannot test all conditions now, put back to pending
                        pending_ready.push_back(index);
                        continue;
                    }
                }
                cond_met = exec_state.condition_met_nodes.contains(index);
                if (cached_node.node->system) {
                    if (cond_met) {
                        dispatch_system(index);
                    } else {
                        exec_state.running_count++;
                        exec_state.finished_queue.push(index);
                    }
                } else {
                    if (exec_state.child_count[index] == 0) {
                        exec_state.finished_queue.push(index);
                    }
                }
                for (const auto& child_index : cached_node.children) {
                    exec_state.condition_met_nodes.set(
                        child_index, cond_met && exec_state.condition_met_nodes.contains(child_index));
                    exec_state.wait_count[child_index]--;
                    if (exec_state.wait_count[child_index] == 0) {
                        ready_stack.push_back(child_index);
                    }
                }
            }
        };

        // loop
        do {
            enter_ready();
            auto finishes = exec_state.finished_queue.try_pop();
            if (finishes.empty()) {
                if (exec_state.running_count == 0) {
                    if (!pending_ready.empty()) {
                        // we have pending, but no ready or running, try to yield and re-enter
                        std::this_thread::yield();
                        continue;
                    }
                    // no running tasks, no finished, no pending, end or deadlock
                    break;
                }
                // wait for some tasks to finish
                finishes = exec_state.finished_queue.pop();
            }

            for (auto&& finished_index : finishes) {
                CachedNode& cached_node = cache->nodes[finished_index];
                if (exec_state.child_count[finished_index] != 0) {
                    // this finished index is pushed by executable task, so the
                    // set is not really finished
                    exec_state.running_count--;
                    exec_state.child_count[finished_index]--;
                    if (exec_state.child_count[finished_index] != 0) {
                        // still has children, set not finished
                        continue;
                    }
                }
                exec_state.finished_nodes.set(finished_index);
                exec_state.entered_nodes.reset(finished_index);
                exec_state.remaining_count--;

                for (const auto& parent_index : cached_node.parents) {
                    exec_state.wait_count[parent_index]--;
                    if (exec_state.wait_count[parent_index] == 0) {
                        exec_state.ready_stack.push_back(parent_index);
                    }
                }
                for (const auto& successor_index : cached_node.successors) {
                    exec_state.wait_count[successor_index]--;
                    if (exec_state.wait_count[successor_index] == 0) {
                        exec_state.ready_stack.push_back(successor_index);
                    }
                }
            }
        } while (true);

        // end apply deferred
        if (config.is_apply_end()) {
            dispatcher.apply_deferred(
                exec_state.finished_nodes.iter_ones() |
                std::views::transform([&](size_t index) -> auto& { return *cache->nodes[index].node->system.get(); }) |
                std::views::filter([&](auto& system) { return system.is_deferred(); }));
        } else if (config.is_queue_deferred()) {
            dispatcher.world_scope([&](World& world) {
                std::ranges::for_each(
                    exec_state.finished_nodes.iter_ones() | std::views::transform([&](size_t index) -> auto& {
                        return *cache->nodes[index].node->system.get();
                    }) | std::views::filter([&](auto& system) { return system.is_deferred(); }),
                    [&](auto& system) { system.queue_deferred(world); });
            });
        }

        if (exec_state.remaining_count > 0) {
            std::println(std::cerr, "Some systems are not executed, check for cycles in the graph.");
            // print state
            ScheduleCache& task_cache = *cache;
            std::println(std::cerr, "Execution state:");
            auto index_to_name = [&](size_t index) -> std::string {
                auto& node = task_cache.nodes[index].node;
                if (node->system) {
                    return std::format("(system: {})", node->system->name());
                } else {
                    return std::format("(set {})", node->label.type_index().short_name());
                }
            };
            std::println(std::cerr, "\tRemaining: {}\tNot Exited: {}",
                         exec_state.finished_nodes.iter_zeros() | std::views::transform(index_to_name),
                         exec_state.entered_nodes.iter_ones() | std::views::transform(index_to_name));
        }
    }
};
}  // namespace epix::core::schedule

namespace epix {
template <typename... Ts>
core::schedule::SetConfig into(Ts&&... ts) {
    return core::schedule::make_sets<true>(std::forward<Ts>(ts)...);
}
template <typename... Ts>
core::schedule::SetConfig sets(Ts&&... ts) {
    return core::schedule::make_sets(std::forward<Ts>(ts)...);
}
}  // namespace epix
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
    return std::make_unique<MultithreadClassicExecutor>();
}

void Schedule::execute(World& world, const ScheduleConfig& config) {
    spdlog::trace("[schedule] Executing schedule '{}'.", label().to_string());
    if (!executor) {
        executor = default_executor();
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

void MultithreadClassicExecutor::execute(ScheduleSystems& _data, World& world, const ExecutorConfig& config) {
    std::shared_ptr cache = _data.cache;  // keep a copy to avoid being invalidated during execution

    // Check if anything to do
    bool has_system = std::ranges::any_of(cache->nodes, [](const CachedNode& cn) { return (bool)cn.node->system; });
    if (!has_system) {
        spdlog::trace("[schedule] No systems to execute, skipping.");
        return;
    }
    spdlog::trace("[schedule] Dispatching {} nodes.", cache->nodes.size());

    // Get thread pool from world resource
    auto& pool = world.resource_or_emplace<ScheduleThreadPool>().pool;

    ExecutionState exec_state{
        .running_count       = 0,
        .remaining_count     = cache->nodes.size(),
        .ready_nodes         = bit_vector(cache->nodes.size()),
        .finished_nodes      = bit_vector(cache->nodes.size()),
        .entered_nodes       = bit_vector(cache->nodes.size()),
        .dependencies        = std::vector<bit_vector>(cache->nodes.size()),
        .children            = std::vector<bit_vector>(cache->nodes.size()),
        .condition_met_nodes = bit_vector(cache->nodes.size(), true),
        .untest_conditions   = std::vector<bit_vector>(cache->nodes.size()),
        .wait_count          = std::vector<size_t>(cache->nodes.size(), 0),
        .child_count         = std::vector<size_t>(cache->nodes.size(), 0),
    };
    for (auto&& [index, cached_node] : cache->nodes | std::views::enumerate) {
        exec_state.wait_count[index]  = cached_node.depends.size() + cached_node.parents.size();
        exec_state.child_count[index] = cached_node.children.size() + (cached_node.node->system ? 1 : 0);
        exec_state.untest_conditions[index].resize(cached_node.node->conditions.size(), true);
        exec_state.dependencies[index].set_range(cached_node.depends, true);
        exec_state.children[index].set_range(cached_node.children, true);
        if (exec_state.wait_count[index] == 0) {
            exec_state.ready_stack.push_back(index);
        }
    }

    // Access tracking for parallel system dispatch
    std::mutex dispatch_mutex;
    std::vector<const FilteredAccessSet*> active_accesses;
    std::vector<size_t> free_slots;

    auto get_slot = [&]() -> size_t {
        // must hold dispatch_mutex
        if (!free_slots.empty()) {
            auto s = free_slots.back();
            free_slots.pop_back();
            return s;
        }
        active_accesses.push_back(nullptr);
        return active_accesses.size() - 1;
    };

    auto is_access_compatible = [&](const FilteredAccessSet& access) -> bool {
        // must hold dispatch_mutex
        return std::ranges::none_of(active_accesses, [&](auto* a) { return a && !access.is_compatible(*a); });
    };

    auto handle_error = [&](size_t index, const RunSystemError& error) {
        if (std::holds_alternative<ValidateParamError>(error)) {
            auto&& param_error = std::get<ValidateParamError>(error);
            spdlog::error("[schedule] parameter validation error at system '{}', type: '{}', msg: {}",
                          cache->nodes[index].node->system->name(), param_error.param_type.short_name(),
                          param_error.message);
        } else if (std::holds_alternative<SystemException>(error)) {
            auto&& expection = std::get<SystemException>(error);
            try {
                std::rethrow_exception(expection.exception);
            } catch (const std::exception& e) {
                spdlog::error("[schedule] system exception at system '{}', msg: {}",
                              cache->nodes[index].node->system->name(), e.what());
            } catch (...) {
                spdlog::error("[schedule] system exception at system '{}', msg: unknown",
                              cache->nodes[index].node->system->name());
            }
        }
        if (config.on_error) config.on_error(error);
    };

    // Pending dispatch queue (systems waiting for access compatibility)
    struct PendingEntry {
        size_t index;
        bool exclusive;  // needs exclusive world access (deferred + ApplyDirect)
    };
    std::deque<PendingEntry> pending_dispatch;

    // Flush pending systems to the pool when access is compatible
    // Must hold dispatch_mutex
    auto flush_pending = [&]() {
        while (!pending_dispatch.empty()) {
            auto& front = pending_dispatch.front();
            if (front.exclusive) {
                // Exclusive: wait for all running pool tasks to drain
                if (std::ranges::any_of(active_accesses, [](auto* a) { return a != nullptr; })) {
                    break;  // something still running, wait
                }
                // Nothing running - run exclusively on caller thread
                auto idx     = front.index;
                auto& system = *cache->nodes[idx].node->system;
                pending_dispatch.pop_front();
                auto res = system.run_no_apply({}, world);
                if (!res) handle_error(idx, res.error());
                system.apply_deferred(world);
                exec_state.finished_queue.push(idx);
                continue;
            }
            auto& access = cache->nodes[front.index].node->system_access;
            if (!is_access_compatible(access)) break;  // conflict with running systems
            auto slot             = get_slot();
            active_accesses[slot] = &access;
            auto idx              = front.index;
            pending_dispatch.pop_front();
            pool.detach_task([&, slot, idx]() {
                auto& system = *cache->nodes[idx].node->system;
                auto res     = system.run_no_apply({}, world);
                if (!res) handle_error(idx, res.error());
                {
                    std::lock_guard lock(dispatch_mutex);
                    active_accesses[slot] = nullptr;
                    free_slots.push_back(slot);
                }
                exec_state.finished_queue.push(idx);
            });
        }
    };

    auto dispatch_system = [&](size_t index) {
        CachedNode& cached_node = cache->nodes[index];
        bool exclusive = (config.deferred == DeferredApply::ApplyDirect && cached_node.node->system->is_deferred());
        {
            std::lock_guard lock(dispatch_mutex);
            pending_dispatch.push_back({index, exclusive});
        }
        exec_state.running_count++;
    };

    std::vector<size_t> pending_ready;  // ready nodes whose conditions can't be tested yet
    auto check_cond = [&](size_t index) -> bool {
        return std::ranges::fold_left(
            exec_state.untest_conditions[index].iter_ones() | std::ranges::to<std::vector>() |
                std::views::transform(
                    [&](size_t i) { return std::make_tuple(i, std::ref(*cache->nodes[index].node->conditions[i])); }),
            true, [&](bool v, auto&& pair) -> bool {
                auto&& [cond_index, condition] = pair;
                auto& access                   = cache->nodes[index].node->condition_access[cond_index];
                {
                    std::lock_guard lock(dispatch_mutex);
                    if (!is_access_compatible(access)) return false;
                }
                // Run condition on caller thread (no conflict with running systems)
                auto res = condition.run({}, world);
                if (res.has_value()) {
                    exec_state.condition_met_nodes.set(index,
                                                       res.value() && exec_state.condition_met_nodes.contains(index));
                    exec_state.untest_conditions[index].reset(cond_index);
                }
                return v && res.has_value();
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
            CachedNode& cached_node = cache->nodes[index];
            bool cond_met           = exec_state.condition_met_nodes.contains(index);
            if (cond_met) {
                bool cond_done = check_cond(index);
                if (!cond_done) {
                    pending_ready.push_back(index);
                    continue;
                }
            }
            cond_met = exec_state.condition_met_nodes.contains(index);
            exec_state.entered_nodes.set(index);
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
                exec_state.condition_met_nodes.set(child_index,
                                                   cond_met && exec_state.condition_met_nodes.contains(child_index));
                exec_state.wait_count[child_index]--;
                if (exec_state.wait_count[child_index] == 0) {
                    ready_stack.push_back(child_index);
                }
            }
        }
        // After processing ready nodes, flush pending dispatch to pool
        {
            std::lock_guard lock(dispatch_mutex);
            flush_pending();
        }
    };

    // Inner execution loop
    do {
        enter_ready();
        auto finishes = exec_state.finished_queue.try_pop();
        if (finishes.empty()) {
            if (exec_state.running_count == 0) {
                if (!pending_ready.empty()) {
                    std::this_thread::yield();
                    continue;
                }
                break;
            }
            finishes = exec_state.finished_queue.pop();
        }

        for (auto&& finished_index : finishes) {
            CachedNode& cached_node = cache->nodes[finished_index];
            if (exec_state.child_count[finished_index] != 0) {
                exec_state.running_count--;
                exec_state.child_count[finished_index]--;
                if (exec_state.child_count[finished_index] != 0) {
                    continue;
                }
            }
            exec_state.finished_nodes.set(finished_index);
            exec_state.entered_nodes.reset(finished_index);
            exec_state.remaining_count--;

            for (const auto& successor_index : cached_node.successors) {
                exec_state.wait_count[successor_index]--;
                exec_state.dependencies[successor_index].reset(finished_index);
                if (exec_state.wait_count[successor_index] == 0) {
                    exec_state.ready_stack.push_back(successor_index);
                }
            }
            for (const auto& parent : cached_node.parents) {
                exec_state.child_count[parent]--;
                exec_state.children[parent].reset(finished_index);
                if (exec_state.child_count[parent] == 0) {
                    exec_state.finished_queue.push(parent);
                }
            }
        }
        // After processing finishes, try to flush newly unblocked pending systems
        {
            std::lock_guard lock(dispatch_mutex);
            flush_pending();
        }
    } while (true);

    // End-of-iteration deferred handling
    switch (config.deferred) {
        case DeferredApply::ApplyEnd:
            for (auto index : exec_state.finished_nodes.iter_ones()) {
                auto& node = cache->nodes[index];
                if (node.node->system && node.node->system->is_deferred()) {
                    node.node->system->apply_deferred(world);
                }
            }
            break;
        case DeferredApply::QueueDeferred:
            for (auto index : exec_state.finished_nodes.iter_ones()) {
                auto& node = cache->nodes[index];
                if (node.node->system && node.node->system->is_deferred()) {
                    node.node->system->queue_deferred(world);
                }
            }
            break;
        case DeferredApply::ApplyDirect:
            break;
        case DeferredApply::Ignore: {
            std::vector<std::shared_ptr<Node>> to_apply;
            to_apply.reserve(exec_state.finished_nodes.size());
            std::ranges::for_each(exec_state.finished_nodes.iter_ones() | std::views::transform([&](size_t index) {
                                      return cache->nodes[index].node;
                                  }) | std::views::filter([&](auto&& node) {
                                      return ((bool)node->system) && node->system.get()->is_deferred();
                                  }),
                                  [&](auto&& node) { to_apply.push_back(node); });
            _data.pending_applies = std::move(to_apply);
        } break;
    }

    if (exec_state.remaining_count > 0) {
        ScheduleCache& task_cache = *cache;
        spdlog::error("Some systems are not executed, check for cycles in the graph. with Execution state:");
        auto index_to_name = [&](size_t index) -> std::string {
            auto& node = task_cache.nodes[index].node;
            if (node->system) {
                return std::format("(system: {})", node->system->name());
            } else {
                return std::format("(set {}#{})", node->label.type_index().short_name(), node->label.extra());
            }
        };
        spdlog::error("\tRemaining: {}\tNot Exited: {}, with remaining depends:{}\n\tand remaining children:{}",
                      exec_state.finished_nodes.iter_zeros() | std::views::transform(index_to_name),
                      exec_state.entered_nodes.iter_ones() | std::views::transform(index_to_name),
                      exec_state.finished_nodes.iter_zeros() | std::views::transform([&](size_t i) {
                          return std::format(
                              "\n\t{}", exec_state.dependencies[i].iter_ones() | std::views::transform(index_to_name));
                      }),
                      exec_state.finished_nodes.iter_zeros() | std::views::transform([&](size_t i) {
                          return std::format("\n\t{}",
                                             exec_state.children[i].iter_ones() | std::views::transform(index_to_name));
                      }));
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

void MultithreadFlatExecutor::rebuild_flat_cache(const std::shared_ptr<ScheduleCache>& cache) {
    m_flat_cache         = std::make_shared<FlatGraphCache>();
    m_flat_cache->source = cache;

    const size_t N = cache->nodes.size();
    m_flat_cache->nodes.resize(2 * N);

    // Initialize flat nodes
    for (size_t i = 0; i < N; i++) {
        const size_t pre_i  = 2 * i;
        const size_t post_i = 2 * i + 1;
        auto& pre           = m_flat_cache->nodes[pre_i];
        auto& post          = m_flat_cache->nodes[post_i];

        pre.original_index = i;
        pre.is_post        = false;
        pre.has_system     = (bool)cache->nodes[i].node->system;

        post.original_index = i;
        post.is_post        = true;
        post.has_system     = false;

        // post_i depends on pre_i
        post.flat_depends.push_back(pre_i);
        pre.flat_successors.push_back(post_i);
    }

    // Build edges from original graph
    for (size_t i = 0; i < N; i++) {
        const auto& cn      = cache->nodes[i];
        const size_t pre_i  = 2 * i;
        const size_t post_i = 2 * i + 1;

        // Dependency edges: this node depends on dep -> pre_i depends on post_dep
        for (size_t dep : cn.depends) {
            const size_t post_dep = 2 * dep + 1;
            m_flat_cache->nodes[pre_i].flat_depends.push_back(post_dep);
            m_flat_cache->nodes[post_dep].flat_successors.push_back(pre_i);
        }

        // Parent-child hierarchy edges:
        //   pre_child depends on pre_parent  (child starts after parent enters)
        //   post_parent depends on post_child (parent exits after all children exit)
        for (size_t par : cn.parents) {
            const size_t pre_par  = 2 * par;
            const size_t post_par = 2 * par + 1;

            m_flat_cache->nodes[pre_i].flat_depends.push_back(pre_par);
            m_flat_cache->nodes[pre_par].flat_successors.push_back(pre_i);

            m_flat_cache->nodes[post_par].flat_depends.push_back(post_i);
            m_flat_cache->nodes[post_i].flat_successors.push_back(post_par);

            m_flat_cache->nodes[pre_i].parent_originals.push_back(par);
        }
    }
}

void MultithreadFlatExecutor::execute(ScheduleSystems& _data, World& world, const ExecutorConfig& config) {
    std::shared_ptr cache = _data.cache;

    // Rebuild flat cache if schedule cache changed
    if (!m_flat_cache || m_flat_cache->source.lock() != cache) {
        rebuild_flat_cache(cache);
    }

    const size_t N          = cache->nodes.size();
    const size_t flat_count = m_flat_cache->nodes.size();

    // Check if anything to do
    bool has_system = std::ranges::any_of(cache->nodes, [](const CachedNode& cn) { return (bool)cn.node->system; });
    if (!has_system) return;

    auto& pool = world.resource_or_emplace<ScheduleThreadPool>().pool;

    // Execution state
    std::vector<size_t> wait_count(flat_count);
    bit_vector finished_flat(flat_count);
    bit_vector condition_met(N, true);
    std::vector<bit_vector> untest_conditions(N);
    async_queue finished_queue;
    std::vector<size_t> ready_stack;
    size_t running_count   = 0;
    size_t remaining_count = flat_count;

    for (size_t fi = 0; fi < flat_count; fi++) {
        wait_count[fi] = m_flat_cache->nodes[fi].flat_depends.size();
        if (wait_count[fi] == 0) {
            ready_stack.push_back(fi);
        }
    }
    for (size_t i = 0; i < N; i++) {
        untest_conditions[i].resize(cache->nodes[i].node->conditions.size(), true);
    }

    // Access tracking for parallel system dispatch
    std::mutex dispatch_mutex;
    std::vector<const FilteredAccessSet*> active_accesses;
    std::vector<size_t> free_slots;

    auto get_slot = [&]() -> size_t {
        if (!free_slots.empty()) {
            auto s = free_slots.back();
            free_slots.pop_back();
            return s;
        }
        active_accesses.push_back(nullptr);
        return active_accesses.size() - 1;
    };

    auto is_access_compatible = [&](const FilteredAccessSet& access) -> bool {
        return std::ranges::none_of(active_accesses, [&](auto* a) { return a && !access.is_compatible(*a); });
    };

    auto handle_error = [&](size_t orig_index, const RunSystemError& error) {
        if (std::holds_alternative<ValidateParamError>(error)) {
            auto&& param_error = std::get<ValidateParamError>(error);
            spdlog::error("[schedule] parameter validation error at system '{}', type: '{}', msg: {}",
                          cache->nodes[orig_index].node->system->name(), param_error.param_type.short_name(),
                          param_error.message);
        } else if (std::holds_alternative<SystemException>(error)) {
            auto&& expection = std::get<SystemException>(error);
            try {
                std::rethrow_exception(expection.exception);
            } catch (const std::exception& e) {
                spdlog::error("[schedule] system exception at system '{}', msg: {}",
                              cache->nodes[orig_index].node->system->name(), e.what());
            } catch (...) {
                spdlog::error("[schedule] system exception at system '{}', msg: unknown",
                              cache->nodes[orig_index].node->system->name());
            }
        }
        if (config.on_error) config.on_error(error);
    };

    struct PendingEntry {
        size_t flat_index;
        size_t original_index;
        bool exclusive;
    };
    std::deque<PendingEntry> pending_dispatch;

    auto flush_pending = [&]() {
        while (!pending_dispatch.empty()) {
            auto& front = pending_dispatch.front();
            if (front.exclusive) {
                if (std::ranges::any_of(active_accesses, [](auto* a) { return a != nullptr; })) {
                    break;
                }
                auto fi      = front.flat_index;
                auto oi      = front.original_index;
                auto& system = *cache->nodes[oi].node->system;
                pending_dispatch.pop_front();
                auto res = system.run_no_apply({}, world);
                if (!res) handle_error(oi, res.error());
                system.apply_deferred(world);
                finished_queue.push(fi);
                continue;
            }
            auto& access = cache->nodes[front.original_index].node->system_access;
            if (!is_access_compatible(access)) break;
            auto slot             = get_slot();
            active_accesses[slot] = &access;
            auto fi               = front.flat_index;
            auto oi               = front.original_index;
            pending_dispatch.pop_front();
            pool.detach_task([&, slot, fi, oi]() {
                auto& system = *cache->nodes[oi].node->system;
                auto res     = system.run_no_apply({}, world);
                if (!res) handle_error(oi, res.error());
                {
                    std::lock_guard lock(dispatch_mutex);
                    active_accesses[slot] = nullptr;
                    free_slots.push_back(slot);
                }
                finished_queue.push(fi);
            });
        }
    };

    auto dispatch_system = [&](size_t flat_index, size_t orig_index) {
        bool exclusive =
            (config.deferred == DeferredApply::ApplyDirect && cache->nodes[orig_index].node->system->is_deferred());
        {
            std::lock_guard lock(dispatch_mutex);
            pending_dispatch.push_back({flat_index, orig_index, exclusive});
        }
        running_count++;
    };

    std::vector<size_t> pending_ready;

    auto check_cond = [&](size_t orig_index) -> bool {
        return std::ranges::fold_left(
            untest_conditions[orig_index].iter_ones() | std::ranges::to<std::vector>() |
                std::views::transform([&](size_t i) {
                    return std::make_tuple(i, std::ref(*cache->nodes[orig_index].node->conditions[i]));
                }),
            true, [&](bool v, auto&& pair) -> bool {
                auto&& [cond_index, condition] = pair;
                auto& access                   = cache->nodes[orig_index].node->condition_access[cond_index];
                {
                    std::lock_guard lock(dispatch_mutex);
                    if (!is_access_compatible(access)) return false;
                }
                auto res = condition.run({}, world);
                if (res.has_value()) {
                    condition_met.set(orig_index, res.value() && condition_met.contains(orig_index));
                    untest_conditions[orig_index].reset(cond_index);
                }
                return v && res.has_value();
            });
    };

    auto enter_ready = [&]() {
        std::swap(pending_ready, ready_stack);
        ready_stack.insert_range(ready_stack.end(), pending_ready);
        pending_ready.clear();

        while (!ready_stack.empty()) {
            size_t fi = ready_stack.back();
            ready_stack.pop_back();
            auto& flat_node = m_flat_cache->nodes[fi];

            if (flat_node.is_post) {
                // Post nodes complete immediately; no work to do.
                running_count++;
                finished_queue.push(fi);
                continue;
            }

            // Pre node
            size_t orig = flat_node.original_index;

            // Inherit condition status from parents
            for (size_t par : flat_node.parent_originals) {
                condition_met.set(orig, condition_met.contains(orig) && condition_met.contains(par));
            }

            bool cond_met = condition_met.contains(orig);
            if (cond_met) {
                bool cond_done = check_cond(orig);
                if (!cond_done) {
                    pending_ready.push_back(fi);
                    continue;
                }
                cond_met = condition_met.contains(orig);
            }

            if (flat_node.has_system) {
                if (cond_met) {
                    dispatch_system(fi, orig);
                } else {
                    running_count++;
                    finished_queue.push(fi);
                }
            } else {
                // Set-only pre node, completes immediately
                running_count++;
                finished_queue.push(fi);
            }
        }

        {
            std::lock_guard lock(dispatch_mutex);
            flush_pending();
        }
    };

    // Main execution loop
    do {
        enter_ready();
        auto finishes = finished_queue.try_pop();
        if (finishes.empty()) {
            if (running_count == 0) {
                if (!pending_ready.empty()) {
                    std::this_thread::yield();
                    continue;
                }
                break;
            }
            finishes = finished_queue.pop();
        }

        for (auto fi : finishes) {
            running_count--;
            remaining_count--;
            finished_flat.set(fi);

            for (size_t succ : m_flat_cache->nodes[fi].flat_successors) {
                wait_count[succ]--;
                if (wait_count[succ] == 0) {
                    ready_stack.push_back(succ);
                }
            }
        }

        {
            std::lock_guard lock(dispatch_mutex);
            flush_pending();
        }
    } while (true);

    // Deferred handling — iterate original nodes that had systems
    switch (config.deferred) {
        case DeferredApply::ApplyEnd:
            for (size_t i = 0; i < N; i++) {
                if (finished_flat.contains(2 * i) && cache->nodes[i].node->system &&
                    cache->nodes[i].node->system->is_deferred()) {
                    cache->nodes[i].node->system->apply_deferred(world);
                }
            }
            break;
        case DeferredApply::QueueDeferred:
            for (size_t i = 0; i < N; i++) {
                if (finished_flat.contains(2 * i) && cache->nodes[i].node->system &&
                    cache->nodes[i].node->system->is_deferred()) {
                    cache->nodes[i].node->system->queue_deferred(world);
                }
            }
            break;
        case DeferredApply::ApplyDirect:
            break;
        case DeferredApply::Ignore: {
            std::vector<std::shared_ptr<Node>> to_apply;
            for (size_t i = 0; i < N; i++) {
                if (finished_flat.contains(2 * i) && cache->nodes[i].node->system &&
                    cache->nodes[i].node->system->is_deferred()) {
                    to_apply.push_back(cache->nodes[i].node);
                }
            }
            _data.pending_applies = std::move(to_apply);
        } break;
    }

    if (remaining_count > 0) {
        spdlog::error("[flat-graph-executor] {} flat nodes were not executed, check for graph issues.",
                      remaining_count);
        auto index_to_name = [&](size_t fi) -> std::string {
            auto& flat_node = m_flat_cache->nodes[fi];
            auto& node      = cache->nodes[flat_node.original_index].node;
            const char* tag = flat_node.is_post ? "post" : "pre";
            if (node->system) {
                return std::format("({} system: {})", tag, node->system->name());
            } else {
                return std::format("({} set {}#{})", tag, node->label.type_index().short_name(), node->label.extra());
            }
        };
        spdlog::error("\tRemaining flat nodes: {}", finished_flat.iter_zeros() | std::views::transform(index_to_name));
    }
}

}  // namespace epix::core

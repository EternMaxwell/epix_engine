module;

#include <spdlog/spdlog.h>

module epix.core;

import std;
import :schedule;

using namespace epix::core;
using namespace executors;

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
                spdlog::trace("[schedule] Running exclusive system '{}' on main thread.", system.name());
                auto res = system.run({}, world);
                if (!res) handle_error(idx, res.error());
                spdlog::trace("[schedule] Finished exclusive system '{}' on main thread.", system.name());
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
                spdlog::trace("[schedule] Running system '{}' on worker thread.", system.name());
                auto res     = system.run_no_apply({}, world);
                if (!res) handle_error(idx, res.error());
                spdlog::trace("[schedule] Finished system '{}' on worker thread.", system.name());
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
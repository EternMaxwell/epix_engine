module;

#include <spdlog/spdlog.h>

module epix.core;

import std;
import :schedule;

using namespace epix::core;
using namespace executors;

void SingleThreadExecutor::execute(ScheduleSystems& _data, World& world, const ExecutorConfig& config) {
    std::shared_ptr cache = _data.cache;  // keep a copy to avoid invalidation during execution

    bool has_system = std::ranges::any_of(cache->nodes, [](const CachedNode& cn) { return (bool)cn.node->system; });
    if (!has_system) {
        spdlog::trace("[schedule] No systems to execute, skipping.");
        return;
    }
    spdlog::trace("[schedule] Single-thread dispatching {} nodes.", cache->nodes.size());

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

    for (auto&& [index, cached_node] : std::views::enumerate(cache->nodes)) {
        exec_state.wait_count[index]  = cached_node.depends.size() + cached_node.parents.size();
        exec_state.child_count[index] = cached_node.children.size() + (cached_node.node->system ? 1 : 0);
        exec_state.untest_conditions[index].resize(cached_node.node->conditions.size(), true);
        exec_state.dependencies[index].set_range(cached_node.depends, true);
        exec_state.children[index].set_range(cached_node.children, true);
        if (exec_state.wait_count[index] == 0) {
            exec_state.ready_stack.push_back(index);
        }
    }

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

    auto dispatch_system = [&](size_t index) {
        CachedNode& cached_node = cache->nodes[index];
        exec_state.running_count++;

        auto& system = *cached_node.node->system;
        spdlog::trace("[schedule] Running system '{}' on main thread.", system.name());
        auto res = system.run_no_apply({}, world);
        if (!res) handle_error(index, res.error());

        if (config.deferred == DeferredApply::ApplyDirect && cached_node.node->system->is_deferred()) {
            cached_node.node->system->apply_deferred(world);
        }
        spdlog::trace("[schedule] Finished system '{}' on main thread.", system.name());

        exec_state.finished_queue.push(index);
    };

    auto check_cond = [&](size_t index) {
        bool all_done = true;
        for (size_t cond_index : exec_state.untest_conditions[index].iter_ones()) {
            auto& condition = *cache->nodes[index].node->conditions[cond_index];
            auto res        = condition.run({}, world);
            if (!res.has_value()) {
                exec_state.condition_met_nodes.reset(index);
                exec_state.untest_conditions[index].reset(cond_index);
                all_done = false;
                continue;
            }
            exec_state.condition_met_nodes.set(index, res.value() && exec_state.condition_met_nodes.contains(index));
            exec_state.untest_conditions[index].reset(cond_index);
        }
        return all_done;
    };

    std::vector<size_t> pending_ready;
    auto enter_ready = [&]() {
        auto& ready_stack = exec_state.ready_stack;
        std::swap(pending_ready, ready_stack);
        ready_stack.insert_range(ready_stack.end(), pending_ready);
        pending_ready.clear();

        while (!ready_stack.empty()) {
            size_t index = ready_stack.back();
            ready_stack.pop_back();
            CachedNode& cached_node = cache->nodes[index];

            bool cond_met = exec_state.condition_met_nodes.contains(index);
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
            } else if (exec_state.child_count[index] == 0) {
                exec_state.finished_queue.push(index);
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
    };

    do {
        enter_ready();
        auto finishes = exec_state.finished_queue.try_pop();
        if (finishes.empty()) {
            if (exec_state.running_count == 0) break;
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
    } while (true);

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
            std::ranges::for_each(std::views::filter(
                                      std::views::transform(exec_state.finished_nodes.iter_ones(), [&](size_t index) {
                                          return cache->nodes[index].node;
                                      }),
                                      [&](auto&& node) {
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
        auto remaining_nodes = std::views::transform(exec_state.finished_nodes.iter_zeros(), index_to_name);
        auto entered_nodes   = std::views::transform(exec_state.entered_nodes.iter_ones(), index_to_name);
        auto remaining_depends = std::views::transform(exec_state.finished_nodes.iter_zeros(), [&](size_t i) {
            return std::format("\n\t{}", std::views::transform(exec_state.dependencies[i].iter_ones(), index_to_name));
        });
        auto remaining_children = std::views::transform(exec_state.finished_nodes.iter_zeros(), [&](size_t i) {
            return std::format("\n\t{}", std::views::transform(exec_state.children[i].iter_ones(), index_to_name));
        });
        spdlog::error("\tRemaining: {}\tNot Exited: {}, with remaining depends:{}\n\tand remaining children:{}",
                      remaining_nodes, entered_nodes, remaining_depends, remaining_children);
    }
}

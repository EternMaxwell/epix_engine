module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

module epix.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :schedule;

using namespace epix::core;
using namespace executors;

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
                spdlog::trace("[schedule] Running exclusive system '{}' on main thread.", system.name());
                auto res = system.run({}, world);
                if (!res) handle_error(oi, res.error());
                spdlog::trace("[schedule] Finished exclusive system '{}' on main thread.", system.name());
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
                spdlog::trace("[schedule] Running system '{}' on worker thread.", system.name());
                auto res = system.run_no_apply({}, world);
                if (!res) handle_error(oi, res.error());
                spdlog::trace("[schedule] Finished system '{}' on worker thread.", system.name());
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
        pending_dispatch.push_back({flat_index, orig_index, exclusive});
        running_count++;
    };

    std::vector<size_t> pending_ready;

    auto check_cond = [&](size_t orig_index) -> bool {
        return std::ranges::fold_left(
            std::views::transform(untest_conditions[orig_index].iter_ones() ,
                [&](size_t i) {
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
        spdlog::error("\tRemaining flat nodes: {}", std::views::transform(finished_flat.iter_zeros(), index_to_name));
    }
}
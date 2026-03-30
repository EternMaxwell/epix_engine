module;

#include <spdlog/spdlog.h>

#include <taskflow/taskflow.hpp>

module epix.core;

import std;

import :schedule;
import :labels;

namespace epix::core {

using namespace executors;

struct AccessEdge {
    size_t from;
    size_t to;
};

struct TaskflowExecutor::Impl {
    tf::Executor executor;
    std::shared_ptr<FlatGraphCache> flat_cache;
    std::weak_ptr<ScheduleCache> source;

    // Cached taskflow — rebuilt whenever flat_cache is rebuilt.
    tf::Taskflow taskflow;
    bool taskflow_built = false;

    // Per-execution state — refreshed by execute() before each run.
    // Raw pointer is safe: execute() holds a shared_ptr to the same cache alive.
    ScheduleCache* exec_cache = nullptr;
    World* exec_world         = nullptr;
    ExecutorConfig exec_config;
    std::vector<uint8_t> condition_met;
    std::mutex system_run_mutex;

    Impl() : executor(std::thread::hardware_concurrency()) {}
    explicit Impl(size_t n) : executor(n) {}

    // ---- flat-cache construction ----

    void rebuild_flat_cache(const std::shared_ptr<ScheduleCache>& cache) {
        flat_cache         = std::make_shared<FlatGraphCache>();
        flat_cache->source = cache;
        source             = cache;

        const size_t N = cache->nodes.size();
        flat_cache->nodes.resize(2 * N);

        for (size_t i = 0; i < N; i++) {
            const size_t pre_i  = 2 * i;
            const size_t post_i = 2 * i + 1;
            auto& pre           = flat_cache->nodes[pre_i];
            auto& post          = flat_cache->nodes[post_i];

            pre.original_index = i;
            pre.is_post        = false;
            pre.has_system     = (bool)cache->nodes[i].node->system;

            post.original_index = i;
            post.is_post        = true;
            post.has_system     = false;

            post.flat_depends.push_back(pre_i);
            pre.flat_successors.push_back(post_i);
        }

        for (size_t i = 0; i < N; i++) {
            const auto& cn      = cache->nodes[i];
            const size_t pre_i  = 2 * i;
            const size_t post_i = 2 * i + 1;

            for (size_t dep : cn.depends) {
                const size_t post_dep = 2 * dep + 1;
                flat_cache->nodes[pre_i].flat_depends.push_back(post_dep);
                flat_cache->nodes[post_dep].flat_successors.push_back(pre_i);
            }

            for (size_t par : cn.parents) {
                const size_t pre_par  = 2 * par;
                const size_t post_par = 2 * par + 1;

                flat_cache->nodes[pre_i].flat_depends.push_back(pre_par);
                flat_cache->nodes[pre_par].flat_successors.push_back(pre_i);

                flat_cache->nodes[post_par].flat_depends.push_back(post_i);
                flat_cache->nodes[post_i].flat_successors.push_back(post_par);

                flat_cache->nodes[pre_i].parent_originals.push_back(par);
            }
        }

        taskflow_built = false;  // invalidate cached taskflow
    }

    bool needs_flat_rebuild(const std::shared_ptr<ScheduleCache>& cache) const {
        return !flat_cache || source.lock() != cache;
    }

    // ---- per-execution helpers (use exec_* members) ----

    std::string node_name(size_t i) {
        auto& node = exec_cache->nodes[i].node;
        if (node->system) return std::string(node->system->name());
        return std::format("set {}#{}", node->label.type_index().short_name(), node->label.extra());
    }

    void handle_error(size_t orig_index, const RunSystemError& error) {
        if (std::holds_alternative<ValidateParamError>(error)) {
            auto&& param_error = std::get<ValidateParamError>(error);
            spdlog::error("[schedule] parameter validation error at system '{}', type: '{}', msg: {}",
                          exec_cache->nodes[orig_index].node->system->name(), param_error.param_type.short_name(),
                          param_error.message);
        } else if (std::holds_alternative<SystemException>(error)) {
            auto&& exception = std::get<SystemException>(error);
            try {
                std::rethrow_exception(exception.exception);
            } catch (const std::exception& e) {
                spdlog::error("[schedule] system exception at system '{}', msg: {}",
                              exec_cache->nodes[orig_index].node->system->name(), e.what());
            } catch (...) {
                spdlog::error("[schedule] system exception at system '{}', msg: unknown",
                              exec_cache->nodes[orig_index].node->system->name());
            }
        }
        if (exec_config.on_error) exec_config.on_error(error);
    }

    void run_system_task_impl(size_t i) {
        auto& system = *exec_cache->nodes[i].node->system;
        spdlog::trace("[taskflow] Running system '{}' (node={}).", system.name(), i);
        bool exclusive =
            system.is_exclusive() || (exec_config.deferred == DeferredApply::ApplyDirect && system.is_deferred());
        {
            std::lock_guard lock(system_run_mutex);
            if (exclusive) {
                auto res = system.run_no_apply({}, *exec_world);
                if (!res) handle_error(i, res.error());
                system.apply_deferred(*exec_world);
            } else {
                auto res = system.run_no_apply({}, *exec_world);
                if (!res) handle_error(i, res.error());
            }
        }
        spdlog::trace("[taskflow] Finished system '{}' (node={}).", system.name(), i);
    }

    // Evaluates parent + own conditions for node i. Sets condition_met[i]=0 on
    // failure. Returns true if the node should run.
    bool eval_conditions(size_t i) {
        for (size_t par : flat_cache->nodes[2 * i].parent_originals) {
            if (!condition_met[par]) {
                condition_met[i] = 0;
                spdlog::trace("[taskflow] Node {} ('{}') skipped due to parent condition.", i, node_name(i));
                return false;
            }
        }
        auto& conditions = exec_cache->nodes[i].node->conditions;
        for (size_t ci = 0; ci < conditions.size(); ci++) {
            auto res = conditions[ci]->run({}, *exec_world);
            if (!res.has_value()) {
                condition_met[i] = 0;
                spdlog::trace("[taskflow] Condition {} on node {} returned no value, skipping.", ci, i);
                return false;
            }
            if (!res.value()) {
                condition_met[i] = 0;
                spdlog::trace("[taskflow] Condition {} on node {} evaluated false, skipping.", ci, i);
                return false;
            }
        }
        return true;
    }

    // ---- taskflow construction ----

    void build_taskflow(const std::shared_ptr<ScheduleCache>& cache) {
        taskflow.clear();

        const size_t N          = cache->nodes.size();
        const size_t flat_count = flat_cache->nodes.size();

        // Build flat-graph adjacency for topo-sort and reachability.
        std::vector<std::vector<size_t>> flat_succs(flat_count);
        std::vector<size_t> indegree(flat_count, 0);
        for (size_t fi = 0; fi < flat_count; fi++) {
            flat_succs[fi] = flat_cache->nodes[fi].flat_successors;
            for (size_t succ : flat_succs[fi]) indegree[succ]++;
        }

        std::vector<size_t> topo_rank(flat_count, flat_count);
        {
            std::deque<size_t> q;
            for (size_t fi = 0; fi < flat_count; fi++) {
                if (indegree[fi] == 0) q.push_back(fi);
            }
            size_t rank = 0;
            while (!q.empty()) {
                size_t u     = q.front();
                topo_rank[u] = rank++;
                q.pop_front();
                for (size_t v : flat_succs[u]) {
                    if (--indegree[v] == 0) q.push_back(v);
                }
            }
        }

        // Collect access sets for each original pre-node.
        std::vector<std::vector<const FilteredAccessSet*>> pre_accesses(N);
        for (size_t i = 0; i < N; i++) {
            auto& cn = cache->nodes[i];
            for (size_t ci = 0; ci < cn.node->condition_access.size(); ci++) {
                pre_accesses[i].push_back(&cn.node->condition_access[ci]);
            }
            if (cn.node->system) pre_accesses[i].push_back(&cn.node->system_access);
        }

        // Reachability from each pre-node through the full flat graph.
        std::vector<bit_vector> reachable_pre(N, bit_vector(N));
        for (size_t i = 0; i < N; i++) {
            bit_vector visited(flat_count);
            std::vector<size_t> stack;
            stack.push_back(2 * i);
            visited.set(2 * i);
            while (!stack.empty()) {
                size_t u = stack.back();
                stack.pop_back();
                for (size_t v : flat_succs[u]) {
                    if (!visited.contains(v)) {
                        visited.set(v);
                        stack.push_back(v);
                    }
                }
            }
            for (size_t j = 0; j < N; j++) {
                if (visited.contains(2 * j)) reachable_pre[i].set(j);
            }
        }

        // Compute access-conflict edges, ordered by topological rank.
        std::vector<AccessEdge> access_edges;
        for (size_t i = 0; i < N; i++) {
            if (pre_accesses[i].empty()) continue;
            for (size_t j = i + 1; j < N; j++) {
                if (pre_accesses[j].empty()) continue;
                if (reachable_pre[i].contains(j) || reachable_pre[j].contains(i)) continue;
                bool conflict = false;
                for (auto* a : pre_accesses[i]) {
                    for (auto* b : pre_accesses[j]) {
                        if (!a->is_compatible(*b)) {
                            conflict = true;
                            break;
                        }
                    }
                    if (conflict) break;
                }
                if (conflict) {
                    if (topo_rank[2 * i] <= topo_rank[2 * j]) {
                        access_edges.push_back({i, j});
                    } else {
                        access_edges.push_back({j, i});
                    }
                }
            }
        }

        spdlog::trace("[taskflow] Building taskflow: {} nodes, {} flat nodes, {} access edges.", N, flat_count,
                      access_edges.size());

        // Create task placeholders.
        std::vector<tf::Task> pre_start_tasks(N);
        std::vector<tf::Task> pre_done_tasks(N);
        std::vector<tf::Task> cond_tasks(N);
        std::vector<tf::Task> system_tasks(N);
        std::vector<tf::Task> post_tasks(N);

        auto pre_start_task = [&](size_t i) -> tf::Task& { return pre_start_tasks[i]; };
        auto pre_done_task  = [&](size_t i) -> tf::Task& { return pre_done_tasks[i]; };

        // Build per-node task chains.
        for (size_t i = 0; i < N; i++) {
            auto& cn            = cache->nodes[i];
            bool has_conditions = !cn.node->conditions.empty();
            bool has_parents    = !flat_cache->nodes[2 * i].parent_originals.empty();
            bool needs_cond     = has_conditions || has_parents;
            bool has_system_i   = (bool)cn.node->system;

            pre_start_tasks[i] = taskflow.placeholder();
            pre_done_tasks[i]  = taskflow.placeholder();
            post_tasks[i]      = taskflow.placeholder();
            pre_done_tasks[i].precede(post_tasks[i]);

            if (needs_cond) {
                if (has_system_i) {
                    // Condition task returns 0=skip (-> pre_done), 1=run (-> system).
                    cond_tasks[i] = taskflow.emplace([this, i]() -> int { return eval_conditions(i) ? 1 : 0; });
                    pre_start_tasks[i].precede(cond_tasks[i]);
                    system_tasks[i] = taskflow.emplace([this, i]() { run_system_task_impl(i); });
                    cond_tasks[i].precede(pre_done_tasks[i], system_tasks[i]);
                    system_tasks[i].precede(pre_done_tasks[i]);
                } else {
                    // Set node: evaluate conditions but no system to branch to.
                    cond_tasks[i] = taskflow.emplace([this, i]() { eval_conditions(i); });
                    pre_start_tasks[i].precede(cond_tasks[i]);
                    cond_tasks[i].precede(pre_done_tasks[i]);
                }
            } else if (has_system_i) {
                system_tasks[i] = taskflow.emplace([this, i]() { run_system_task_impl(i); });
                pre_start_tasks[i].precede(system_tasks[i]);
                system_tasks[i].precede(pre_done_tasks[i]);
            } else {
                // Empty set node.
                pre_start_tasks[i].precede(pre_done_tasks[i]);
            }
        }

        // Wire dependency and hierarchy edges.
        for (size_t i = 0; i < N; i++) {
            auto& cn = cache->nodes[i];
            for (size_t dep : cn.depends) {
                post_tasks[dep].precede(pre_start_task(i));
            }
            for (size_t par : flat_cache->nodes[2 * i].parent_originals) {
                pre_done_task(par).precede(pre_start_task(i));
                post_tasks[i].precede(post_tasks[par]);
            }
        }

        // Wire access-conflict edges.
        for (auto& [from, to] : access_edges) {
            post_tasks[from].precede(pre_start_task(to));
        }

        condition_met.assign(N, 1);
        taskflow_built = true;
    }
};

TaskflowExecutor::TaskflowExecutor() : m_impl(std::make_unique<Impl>()) {}
TaskflowExecutor::TaskflowExecutor(size_t num_threads) : m_impl(std::make_unique<Impl>(num_threads)) {}
TaskflowExecutor::~TaskflowExecutor()                                      = default;
TaskflowExecutor::TaskflowExecutor(TaskflowExecutor&&) noexcept            = default;
TaskflowExecutor& TaskflowExecutor::operator=(TaskflowExecutor&&) noexcept = default;

void TaskflowExecutor::execute(ScheduleSystems& _data, World& world, const ExecutorConfig& config) {
    std::shared_ptr cache = _data.cache;

    if (m_impl->needs_flat_rebuild(cache)) {
        m_impl->rebuild_flat_cache(cache);
    }

    const size_t N = cache->nodes.size();

    bool has_system = std::ranges::any_of(cache->nodes, [](const CachedNode& cn) { return (bool)cn.node->system; });
    if (!has_system) return;

    if (!m_impl->taskflow_built) {
        m_impl->build_taskflow(cache);
    }

    // Refresh per-execution state.
    m_impl->exec_cache  = cache.get();
    m_impl->exec_world  = &world;
    m_impl->exec_config = config;
    std::fill(m_impl->condition_met.begin(), m_impl->condition_met.end(), uint8_t{1});

    spdlog::trace("[taskflow] Executing schedule cache with {} nodes.", N);

    m_impl->executor.run(m_impl->taskflow).wait();

    // End-of-iteration deferred handling.
    auto& condition_met = m_impl->condition_met;
    switch (config.deferred) {
        case DeferredApply::ApplyEnd:
            for (size_t i = 0; i < N; i++) {
                if (condition_met[i] && cache->nodes[i].node->system && cache->nodes[i].node->system->is_deferred()) {
                    cache->nodes[i].node->system->apply_deferred(world);
                }
            }
            break;
        case DeferredApply::QueueDeferred:
            for (size_t i = 0; i < N; i++) {
                if (condition_met[i] && cache->nodes[i].node->system && cache->nodes[i].node->system->is_deferred()) {
                    cache->nodes[i].node->system->queue_deferred(world);
                }
            }
            break;
        case DeferredApply::ApplyDirect:
            break;
        case DeferredApply::Ignore: {
            std::vector<std::shared_ptr<Node>> to_apply;
            for (size_t i = 0; i < N; i++) {
                if (condition_met[i] && cache->nodes[i].node->system && cache->nodes[i].node->system->is_deferred()) {
                    to_apply.push_back(cache->nodes[i].node);
                }
            }
            _data.pending_applies = std::move(to_apply);
        } break;
    }
}

}  // namespace epix::core

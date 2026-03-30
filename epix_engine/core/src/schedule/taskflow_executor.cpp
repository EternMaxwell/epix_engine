module;

#include <spdlog/spdlog.h>

#include <taskflow/taskflow.hpp>


module epix.core;

import std;

import :schedule;
import :labels;

namespace epix::core {

struct TaskflowExecutor::Impl {
    tf::Executor executor;
    std::shared_ptr<FlatGraphCache> flat_cache;
    std::weak_ptr<ScheduleCache> source;

    Impl() : executor(std::thread::hardware_concurrency()) {}
    explicit Impl(size_t n) : executor(n) {}

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
    }

    bool needs_rebuild(const std::shared_ptr<ScheduleCache>& cache) const {
        return !flat_cache || source.lock() != cache;
    }
};

TaskflowExecutor::TaskflowExecutor() : m_impl(std::make_unique<Impl>()) {}
TaskflowExecutor::TaskflowExecutor(size_t num_threads) : m_impl(std::make_unique<Impl>(num_threads)) {}
TaskflowExecutor::~TaskflowExecutor()                                      = default;
TaskflowExecutor::TaskflowExecutor(TaskflowExecutor&&) noexcept            = default;
TaskflowExecutor& TaskflowExecutor::operator=(TaskflowExecutor&&) noexcept = default;

// Collect all FilteredAccessSet pointers that a pre-node touches (conditions + system).
static void collect_pre_node_accesses(const CachedNode& cn, std::vector<const FilteredAccessSet*>& out) {
    for (size_t ci = 0; ci < cn.node->condition_access.size(); ci++) {
        out.push_back(&cn.node->condition_access[ci]);
    }
    if (cn.node->system) {
        out.push_back(&cn.node->system_access);
    }
}

// Check if any access in lhs conflicts with any access in rhs.
static bool accesses_conflict(const std::vector<const FilteredAccessSet*>& lhs,
                              const std::vector<const FilteredAccessSet*>& rhs) {
    for (auto* a : lhs) {
        for (auto* b : rhs) {
            if (!a->is_compatible(*b)) return true;
        }
    }
    return false;
}

void TaskflowExecutor::execute(ScheduleSystems& _data, World& world, const ExecutorConfig& config) {
    std::shared_ptr cache = _data.cache;

    if (m_impl->needs_rebuild(cache)) {
        m_impl->rebuild_flat_cache(cache);
    }

    const size_t N          = cache->nodes.size();
    const size_t flat_count = m_impl->flat_cache->nodes.size();

    bool has_system = std::ranges::any_of(cache->nodes, [](const CachedNode& cn) { return (bool)cn.node->system; });
    if (!has_system) return;

    auto& flat_cache = *m_impl->flat_cache;

    // Collect access sets for each pre-node (for access-conflict edge computation).
    std::vector<std::vector<const FilteredAccessSet*>> pre_accesses(N);
    for (size_t i = 0; i < N; i++) {
        collect_pre_node_accesses(cache->nodes[i], pre_accesses[i]);
    }

    // Build reachability to avoid adding redundant access-conflict edges.
    // reachable[i] = set of pre-node original indices that are transitively
    // reachable from pre-node i via the flat graph.
    // We compute this on original indices only (pre nodes), since post nodes
    // are just synchronization.
    // First compute direct execution ordering (A must finish before B):
    // an original index j is ordered before i if pre_i transitively depends on post_j
    // in the flat graph. For simplicity we compute reachability on original indices.
    std::vector<bit_vector> reachable(N, bit_vector(N));
    {
        // Build adjacency from flat graph: original j -> original i
        // if pre_i (directly or transitively) depends on post_j.
        // Direct flat edges encode this: if pre_i depends on post_j, then j -> i.
        std::vector<std::vector<size_t>> orig_succs(N);
        for (size_t i = 0; i < N; i++) {
            const size_t pre_i = 2 * i;
            for (size_t dep_fi : flat_cache.nodes[pre_i].flat_depends) {
                if (flat_cache.nodes[dep_fi].is_post) {
                    size_t j = flat_cache.nodes[dep_fi].original_index;
                    if (j != i) orig_succs[j].push_back(i);
                }
            }
        }
        // Topological DFS to compute transitive closure
        std::vector<uint8_t> visited(N, 0);
        std::function<void(size_t)> dfs = [&](size_t u) {
            if (visited[u]) return;
            visited[u] = 1;
            for (size_t v : orig_succs[u]) {
                dfs(v);
                reachable[u].set(v);
                reachable[u].union_with(reachable[v]);
            }
        };
        for (size_t i = 0; i < N; i++) dfs(i);
    }

    // Compute access-conflict edges between pre-nodes.
    // For each unordered pair (i, j) where accesses conflict, add an edge.
    // To avoid cycles, always order by index: i -> j where i < j,
    // but skip if already transitively ordered.
    struct AccessEdge {
        size_t from;
        size_t to;
    };
    std::vector<AccessEdge> access_edges;
    for (size_t i = 0; i < N; i++) {
        if (pre_accesses[i].empty()) continue;
        for (size_t j = i + 1; j < N; j++) {
            if (pre_accesses[j].empty()) continue;
            // Already ordered?
            if (reachable[i].contains(j) || reachable[j].contains(i)) continue;
            if (accesses_conflict(pre_accesses[i], pre_accesses[j])) {
                access_edges.push_back({i, j});
            }
        }
    }

    // Per-original-node condition state.
    // Written only by its own condition task; dependency edges guarantee visibility.
    std::vector<uint8_t> condition_met(N, 1);

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

    // Build the taskflow graph.
    // For each original node i we create up to 3 tf::Task entries:
    //   cond_tasks[i]   - condition task (returns 0=skip, 1=run) if node has conditions or parents with conditions
    //   system_tasks[i] - system execution task (only if node has a system)
    //   post_tasks[i]   - synchronization placeholder  (always)
    //
    // Graph wiring:
    //   cond_tasks[i].precede(post_tasks[i] /*skip=0*/, system_tasks[i] or post_tasks[i] /*run=1*/)
    //   system_tasks[i] -> post_tasks[i]
    //   hierarchy/dependency edges on pre/post as before
    //   access-conflict edges: post_tasks[from] -> cond/system_tasks[to]

    tf::Taskflow taskflow;
    std::vector<tf::Task> cond_tasks(N);
    std::vector<tf::Task> system_tasks(N);
    std::vector<tf::Task> post_tasks(N);
    std::vector<bool> has_cond(N, false);
    std::vector<bool> has_sys(N, false);

    for (size_t i = 0; i < N; i++) {
        auto& cn            = cache->nodes[i];
        bool has_conditions = !cn.node->conditions.empty();
        bool has_parents    = !flat_cache.nodes[2 * i].parent_originals.empty();
        bool needs_cond     = has_conditions || has_parents;
        bool has_system_i   = (bool)cn.node->system;

        has_cond[i] = needs_cond;
        has_sys[i]  = has_system_i;

        // Post task: always a placeholder
        post_tasks[i] = taskflow.placeholder();

        if (needs_cond) {
            // Condition task: returns 0 to skip (go to post), 1 to proceed (go to system/post)
            cond_tasks[i] = taskflow.emplace([&, i]() -> int {
                // Inherit condition from parents
                for (size_t par : flat_cache.nodes[2 * i].parent_originals) {
                    if (!condition_met[par]) {
                        condition_met[i] = 0;
                        return 0;  // skip
                    }
                }
                // Evaluate own conditions
                auto& conditions = cache->nodes[i].node->conditions;
                for (size_t ci = 0; ci < conditions.size(); ci++) {
                    auto res = conditions[ci]->run({}, world);
                    if (res.has_value() && !res.value()) {
                        condition_met[i] = 0;
                        return 0;  // skip
                    }
                }
                return 1;  // proceed
            });

            if (has_system_i) {
                // skip=0 -> post, run=1 -> system
                system_tasks[i] = taskflow.emplace([&, i]() {
                    auto& system   = *cache->nodes[i].node->system;
                    bool exclusive = (config.deferred == DeferredApply::ApplyDirect && system.is_deferred());
                    if (exclusive) {
                        auto res = system.run_no_apply({}, world);
                        if (!res) handle_error(i, res.error());
                        system.apply_deferred(world);
                    } else {
                        auto res = system.run_no_apply({}, world);
                        if (!res) handle_error(i, res.error());
                    }
                });
                cond_tasks[i].precede(post_tasks[i], system_tasks[i]);  // 0->post, 1->system
                system_tasks[i].precede(post_tasks[i]);
            } else {
                // No system: skip=0 -> post, run=1 -> post (both go to post)
                cond_tasks[i].precede(post_tasks[i], post_tasks[i]);
            }
        } else if (has_system_i) {
            // No condition, has system
            system_tasks[i] = taskflow.emplace([&, i]() {
                auto& system   = *cache->nodes[i].node->system;
                bool exclusive = (config.deferred == DeferredApply::ApplyDirect && system.is_deferred());
                if (exclusive) {
                    auto res = system.run_no_apply({}, world);
                    if (!res) handle_error(i, res.error());
                    system.apply_deferred(world);
                } else {
                    auto res = system.run_no_apply({}, world);
                    if (!res) handle_error(i, res.error());
                }
            });
            system_tasks[i].precede(post_tasks[i]);
        }
        // else: empty set node, post_tasks[i] is the only task
    }

    // Helper to get the "entry" task for a node (what predecessors should precede)
    auto entry_task = [&](size_t i) -> tf::Task& {
        if (has_cond[i]) return cond_tasks[i];
        if (has_sys[i]) return system_tasks[i];
        return post_tasks[i];
    };

    // Wire flat graph structural edges (dependency + hierarchy)
    for (size_t i = 0; i < N; i++) {
        auto& cn = cache->nodes[i];

        // Dependency: post_dep -> entry(i)
        for (size_t dep : cn.depends) {
            post_tasks[dep].precede(entry_task(i));
        }

        // Parent-child:
        //   entry(parent) -> entry(child)  [parent enters before child]
        //   post(child) -> post(parent)    [parent waits for child to finish]
        for (size_t par : flat_cache.nodes[2 * i].parent_originals) {
            entry_task(par).precede(entry_task(i));
            post_tasks[i].precede(post_tasks[par]);
        }
    }

    // Wire access-conflict edges: post_tasks[from] -> entry_task(to)
    for (auto& [from, to] : access_edges) {
        post_tasks[from].precede(entry_task(to));
    }

    // Execute and wait
    m_impl->executor.run(taskflow).wait();

    // End-of-iteration deferred handling
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

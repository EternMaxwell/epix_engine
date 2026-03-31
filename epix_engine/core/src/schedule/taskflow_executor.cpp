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
    std::optional<tf::Taskflow> taskflow;

    World* exec_world = nullptr;
    ExecutorConfig exec_config;

    std::shared_ptr<FlatGraphCache> flat_cache;
    std::shared_ptr<ScheduleCache> source;
    std::vector<std::uint8_t> entered_nodes;
    std::vector<std::uint8_t> ended_nodes;

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
    }

    bool needs_flat_rebuild(const std::shared_ptr<ScheduleCache>& cache) const {
        return !flat_cache || source != cache;
    }

    void handle_error(size_t index, const RunSystemError& error) {
        if (std::holds_alternative<ValidateParamError>(error)) {
            auto&& param_error = std::get<ValidateParamError>(error);
            spdlog::error("[schedule] parameter validation error at system '{}', type: '{}', msg: {}",
                          source->nodes[index].node->system->name(), param_error.param_type.short_name(),
                          param_error.message);
        } else if (std::holds_alternative<SystemException>(error)) {
            auto&& expection = std::get<SystemException>(error);
            try {
                std::rethrow_exception(expection.exception);
            } catch (const std::exception& e) {
                spdlog::error("[schedule] system exception at system '{}', msg: {}",
                              source->nodes[index].node->system->name(), e.what());
            } catch (...) {
                spdlog::error("[schedule] system exception at system '{}', msg: unknown",
                              source->nodes[index].node->system->name());
            }
        }
        if (exec_config.on_error) exec_config.on_error(error);
    };

    // ---- taskflow construction ----

    void build_taskflow(const std::shared_ptr<ScheduleCache>& cache) {
        this->taskflow.reset();
        this->taskflow.emplace();
        auto& taskflow = *this->taskflow;

        struct TaskNode {
            std::shared_ptr<Node> node;
            std::optional<tf::Task> task;      // system task, if has
            std::vector<tf::Task> cond_tasks;  // condition tasks, one per condition
            tf::Task cond_meet;  // placeholder task that represents the point where all conditions are met; conditions
                                 // precede this, system task (if has) follows this
            tf::Task in;         // reference to a task that can be used to configure tasks that before this node.
            tf::Task out;        // reference to a task that can be used to configure tasks that after this node.
            bool exclusive = false;
        };

        auto node_name = [&](size_t i) {
            auto& node = cache->nodes[i].node;
            if (node->system) {
                return std::format("system '{}' (node {})", node->system->name(), i);
            } else {
                return std::format("set '{}' (node {})", node->label.to_string(), i);
            }
        };

        std::vector<TaskNode> task_nodes(cache->nodes.size());
        for (std::size_t i = 0; i < cache->nodes.size(); i++) {
            bool exclusive =
                cache->nodes[i].node->system &&
                (cache->nodes[i].node->system->is_exclusive() ||
                 (exec_config.deferred == DeferredApply::ApplyDirect && cache->nodes[i].node->system->is_deferred()));
            task_nodes[i].node      = cache->nodes[i].node;
            task_nodes[i].exclusive = exclusive;
            if (cache->nodes[i].node->system) {
                task_nodes[i].task.emplace(
                    taskflow
                        .emplace([this, i, node = cache->nodes[i].node, exclusive]() {
                            spdlog::trace("[taskflow] Running system '{}' (node={}).", node->system->name(), i);
                            if (!exclusive) {
                                auto res = node->system->run_no_apply({}, *exec_world);
                                if (!res) this->handle_error(i, res.error());
                            } else {
                                auto res = node->system->run({}, *exec_world);
                                if (!res) this->handle_error(i, res.error());
                            }
                            spdlog::trace("[taskflow] Finished system '{}' (node={}).", node->system->name(), i);
                        })
                        .name(node_name(i)));
            }
            auto& conditions = cache->nodes[i].node->conditions;
            task_nodes[i].cond_tasks.reserve(conditions.size());
            for (std::size_t ci = 0; ci < conditions.size(); ci++) {
                task_nodes[i].cond_tasks.emplace_back(
                    taskflow
                        .emplace([this, i, ci, node = cache->nodes[i].node]() {
                            auto res = node->conditions[ci]->run({}, *exec_world);
                            if (!res.has_value()) {
                                spdlog::trace("[taskflow] Condition {} on node {} returned no value, skipping.", ci, i);
                            } else if (!res.value()) {
                                spdlog::trace("[taskflow] Condition {} on node {} evaluated false, skipping.", ci, i);
                            }
                            return res.value_or(false);
                        })
                        .name(std::format("{} [cond {}]", node_name(i), ci)));
            }
            if (!task_nodes[i].cond_tasks.empty()) {
                task_nodes[i].in        = task_nodes[i].cond_tasks[0];
                task_nodes[i].out       = taskflow.placeholder().name(node_name(i) + " [placeholder out]");
                task_nodes[i].cond_meet = taskflow.placeholder().name(node_name(i) + " [conditions met]");
                if (task_nodes[i].task.has_value()) {
                    task_nodes[i].cond_meet.precede(task_nodes[i].task.value());
                    task_nodes[i].task.value().precede(task_nodes[i].out);
                } else {
                    task_nodes[i].cond_meet.precede(task_nodes[i].out);
                }
            } else if (task_nodes[i].task.has_value() && cache->nodes[i].children.empty()) {
                task_nodes[i].in        = task_nodes[i].task.value();
                task_nodes[i].out       = task_nodes[i].task.value();
                task_nodes[i].cond_meet = task_nodes[i].task.value();
            } else {
                task_nodes[i].in        = taskflow.placeholder().name(node_name(i) + " [placeholder in]");
                task_nodes[i].out       = taskflow.placeholder().name(node_name(i) + " [placeholder out]");
                task_nodes[i].cond_meet = task_nodes[i].in;
                if (task_nodes[i].task.has_value()) {
                    task_nodes[i].cond_meet.precede(task_nodes[i].task.value());
                    task_nodes[i].task.value().precede(task_nodes[i].out);
                } else {
                    task_nodes[i].cond_meet.precede(task_nodes[i].out);
                }
            }
            task_nodes[i].in.precede(taskflow
                                         .emplace([i, this] {
                                             spdlog::trace("[taskflow] Entered node {}.", i);
                                             entered_nodes[i] = 1;
                                         })
                                         .name(node_name(i) + " [enter]"));
            task_nodes[i].out.precede(taskflow
                                          .emplace([i, this] {
                                              spdlog::trace("[taskflow] Ended node {}.", i);
                                              ended_nodes[i] = 1;
                                          })
                                          .name(node_name(i) + " [end]"));
            // configure deps between conditions and system
            for (std::size_t ci = 0; ci < conditions.size(); ci++) {
                task_nodes[i].cond_tasks[ci].precede(task_nodes[i].out, ci == conditions.size() - 1
                                                                            ? task_nodes[i].cond_meet
                                                                            : task_nodes[i].cond_tasks[ci + 1]);
            }
        }

        // configure edges between nodes
        for (std::size_t i = 0; i < cache->nodes.size(); i++) {
            auto&& task_node = task_nodes[i];
            for (std::size_t dep : cache->nodes[i].depends) {
                task_nodes[dep].out.precede(task_node.in);
            }
            for (std::size_t par : cache->nodes[i].parents) {
                // after par cond met
                task_nodes[par].cond_meet.precede(task_node.in);
                task_node.out.precede(task_nodes[par].out);
            }
        }

        // configure edges between no explicit deps but access conflict tasks.
        std::vector<std::tuple<tf::Task, FilteredAccessSet*, bool>> task_accesses;
        std::unordered_map<tf::Task, std::optional<size_t>> task_to_node;
        for (std::size_t i = 0; i < cache->nodes.size(); i++) {
            if (cache->nodes[i].node->system) {
                task_accesses.emplace_back(task_nodes[i].cond_meet, &cache->nodes[i].node->system_access,
                                           task_nodes[i].exclusive);
                task_to_node[task_nodes[i].cond_meet] = task_accesses.size() - 1;
            }
            for (std::size_t ci = 0; ci < cache->nodes[i].node->conditions.size(); ci++) {
                task_accesses.emplace_back(task_nodes[i].cond_tasks[ci], &cache->nodes[i].node->condition_access[ci],
                                           false);
                task_to_node[task_nodes[i].cond_tasks[ci]] = task_accesses.size() - 1;
            }
        }
        // tasks that are not from systems or conditions (e.g. placeholders) are not in task_to_node and thus not in
        // task_accesses, so we don't consider them for access conflict edges.
        taskflow.for_each_task([&](tf::Task t) {
            if (!task_to_node.contains(t)) {
                task_to_node[t] = std::nullopt;
            }
        });
        // topo sort the task accesses
        std::vector<std::size_t> access_order;
        access_order.reserve(task_accesses.size());
        std::unordered_set<tf::Task> visited;
        for (auto&& [task, index] : task_to_node) {
            if (task.num_successors() == 0) {
                if (index) access_order.push_back(*index);
                visited.insert(task);
            }
        }
        while (access_order.size() < task_accesses.size()) {
            bool progress = false;
            for (auto&& [task, index] : task_to_node) {
                if (visited.contains(task)) continue;
                bool ready = true;
                task.for_each_successor([&](tf::Task succ) {
                    if (!visited.contains(succ)) {
                        ready = false;
                    }
                });
                if (ready) {
                    if (index) access_order.push_back(*index);
                    visited.insert(task);
                    progress = true;
                }
            }
            if (!progress) {
                throw std::runtime_error("cycle detected in task accesses");
            }
        }

        // order done, add edges for access conflicts
        for (std::size_t i1 = 0; i1 < access_order.size(); i1++) {
            auto&& [task1, access1, exclusive1] = task_accesses[access_order[i1]];
            for (std::size_t i2 = i1 + 1; i2 < access_order.size(); i2++) {
                auto&& [task2, access2, exclusive2] = task_accesses[access_order[i2]];
                if (exclusive1 || exclusive2 || !access1->is_compatible(*access2)) {
                    task1.precede(task2);
                }
            }
        }
    }
};

TaskflowExecutor::TaskflowExecutor() : m_impl(std::make_unique<Impl>()) {}
TaskflowExecutor::TaskflowExecutor(size_t num_threads) : m_impl(std::make_unique<Impl>(num_threads)) {}
TaskflowExecutor::~TaskflowExecutor()                                      = default;
TaskflowExecutor::TaskflowExecutor(TaskflowExecutor&&) noexcept            = default;
TaskflowExecutor& TaskflowExecutor::operator=(TaskflowExecutor&&) noexcept = default;

void TaskflowExecutor::execute(ScheduleSystems& _data, World& world, const ExecutorConfig& config) {
    std::shared_ptr cache = _data.cache;

    bool rebuild_flat = m_impl->needs_flat_rebuild(cache);
    bool rebuild_taskflow =
        rebuild_flat || !m_impl->taskflow.has_value() || config.deferred != m_impl->exec_config.deferred;

    m_impl->source      = cache;
    m_impl->exec_world  = &world;
    m_impl->exec_config = config;

    if (rebuild_flat) {
        m_impl->rebuild_flat_cache(cache);
    }
    if (rebuild_taskflow) {
        m_impl->build_taskflow(m_impl->flat_cache->source.lock());
    }

    bool has_system = std::ranges::any_of(cache->nodes, [](const CachedNode& cn) { return (bool)cn.node->system; });
    if (!has_system) return;

    m_impl->entered_nodes.resize(cache->nodes.size(), 0);
    m_impl->ended_nodes.resize(cache->nodes.size(), 0);

    m_impl->executor.run(m_impl->taskflow.value()).wait();

    // End-of-iteration deferred handling.
    switch (config.deferred) {
        case DeferredApply::ApplyEnd:
            for (size_t i = 0; i < cache->nodes.size(); i++) {
                if (cache->nodes[i].node->system && cache->nodes[i].node->system->is_deferred()) {
                    cache->nodes[i].node->system->apply_deferred(world);
                }
            }
            break;
        case DeferredApply::QueueDeferred:
            for (size_t i = 0; i < cache->nodes.size(); i++) {
                if (cache->nodes[i].node->system && cache->nodes[i].node->system->is_deferred()) {
                    cache->nodes[i].node->system->queue_deferred(world);
                }
            }
            break;
        case DeferredApply::ApplyDirect:
            break;
        case DeferredApply::Ignore: {
            std::vector<std::shared_ptr<Node>> to_apply;
            for (size_t i = 0; i < cache->nodes.size(); i++) {
                if (cache->nodes[i].node->system && cache->nodes[i].node->system->is_deferred()) {
                    to_apply.push_back(cache->nodes[i].node);
                }
            }
            _data.pending_applies = std::move(to_apply);
        } break;
    }
}

}  // namespace epix::core

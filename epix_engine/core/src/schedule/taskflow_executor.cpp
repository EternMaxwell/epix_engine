module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

#include <taskflow/taskflow.hpp>

module epix.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :schedule;
import :labels;

namespace epix::core {

using namespace executors;

struct AccessEdge {
    size_t from;
    size_t to;
};

// Shared thread pool across all TaskflowExecutor instances.
// Avoids creating N × hardware_concurrency threads when N schedules each have their own executor.
static std::shared_ptr<tf::Executor>& shared_executor(size_t num_threads = 0) {
    static std::shared_ptr<tf::Executor> instance;
    if (!instance) {
        // Cap at a reasonable number — too many threads adds overhead
        // from work-stealing contention for small task graphs.
        size_t n = num_threads > 0 ? num_threads : std::min<size_t>(std::thread::hardware_concurrency(), 4);
        instance = std::make_shared<tf::Executor>(n);
    }
    return instance;
}

struct TaskflowExecutor::Impl {
    std::shared_ptr<tf::Executor> executor;
    std::optional<tf::Taskflow> taskflow;

    World* exec_world = nullptr;
    ExecutorConfig exec_config;

    std::shared_ptr<ScheduleCache> source;
    std::vector<std::uint8_t> cond_met;

    // Cached at build time
    bool cached_has_system = false;

    Impl() : executor(shared_executor()) {}
    explicit Impl(size_t n) : executor(shared_executor(n)) {}

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

        cond_met.resize(cache->nodes.size(), 1);

        // Per-node we produce at most 3 taskflow tasks:
        //   in   – entry point (may alias task or gate)
        //   task – runs the system (may also evaluate conditions/parents for leaf nodes)
        //   out  – exit point; for parent nodes, a join placeholder waiting for children
        //   done – same as out for non-parent nodes; for parent nodes, the join point
        //   cond_decided – for parent nodes, the point after conditions are decided
        //   gate – for parent nodes with conditions, evaluates conditions before task
        //
        // Leaf nodes without conditions/parents: 1 task (in=task=out=done)
        // Leaf nodes with conditions/parents:    1 task (merged gate+system, in=task=out=done)
        // Parent nodes without conditions:       task + done (in=task, out=done)
        // Parent nodes with conditions:          gate + task + done (in=gate, out=done)

        struct TaskNode {
            tf::Task in;
            tf::Task out;
            tf::Task done;
            tf::Task task;          // system task (or placeholder for sets)
            tf::Task gate;          // merged condition+parent gate (only if has_child && has_cond/parent)
            tf::Task cond_decided;  // point where cond_met[i] is finalized (for children to wait on)
            bool exclusive = false;
            FilteredAccessSet gate_access;  // merged access for gate task
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
            auto& cn       = cache->nodes[i];
            bool exclusive = cn.node->system &&
                             (cn.node->system->is_exclusive() ||
                              (exec_config.deferred == DeferredApply::ApplyDirect && cn.node->system->is_deferred()));
            task_nodes[i].exclusive = exclusive;

            bool is_system  = (bool)cn.node->system;
            bool has_child  = !cn.children.empty();
            bool has_cond   = !cn.node->conditions.empty();
            bool has_parent = !cn.parents.empty();
            bool has_gate   = has_cond || has_parent;

            if (!has_child) {
                // ---- LEAF NODE: merge everything into 1 task ----
                if (is_system) {
                    task_nodes[i].task =
                        taskflow
                            .emplace([this, i, node = cn.node, exclusive, has_gate, has_parent,
                                      parents = cn.parents]() {
                                // Parent check
                                if (has_parent) {
                                    for (size_t par : parents) {
                                        if (!this->cond_met[par]) {
                                            this->cond_met[i] = 0;
                                            return;
                                        }
                                    }
                                }
                                // Evaluate all conditions
                                if (has_gate) {
                                    for (auto& cond : node->conditions) {
                                        if (!cond->run({}, *exec_world).value_or(false)) {
                                            this->cond_met[i] = 0;
                                            return;
                                        }
                                    }
                                }
                                // Run system
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
                            .name(node_name(i));
                } else {
                    // Set node (no system): still need to evaluate conditions/parent for cond_met propagation
                    if (has_gate) {
                        task_nodes[i].task =
                            taskflow
                                .emplace([this, i, node = cn.node, has_parent, parents = cn.parents]() {
                                    if (has_parent) {
                                        for (size_t par : parents) {
                                            if (!this->cond_met[par]) {
                                                this->cond_met[i] = 0;
                                                return;
                                            }
                                        }
                                    }
                                    for (auto& cond : node->conditions) {
                                        if (!cond->run({}, *exec_world).value_or(false)) {
                                            this->cond_met[i] = 0;
                                            return;
                                        }
                                    }
                                })
                                .name(node_name(i));
                    } else {
                        task_nodes[i].task = taskflow.placeholder().name(node_name(i));
                    }
                }
                // Merged access: combine all condition accesses + system access
                if (has_gate) {
                    for (std::size_t ci = 0; ci < cn.node->conditions.size(); ci++) {
                        task_nodes[i].gate_access.extend(FilteredAccessSet(cn.node->condition_access[ci]));
                    }
                    if (is_system) task_nodes[i].gate_access.extend(FilteredAccessSet(cn.node->system_access));
                }
                task_nodes[i].in = task_nodes[i].out = task_nodes[i].done = task_nodes[i].task;
                task_nodes[i].cond_decided                                = task_nodes[i].task;
            } else {
                // ---- PARENT NODE (has children): need separate cond_decided for children ----
                if (has_gate) {
                    // Gate task: evaluates parent check + all conditions, sets cond_met[i]
                    task_nodes[i].gate = taskflow
                                             .emplace([this, i, node = cn.node, has_parent, parents = cn.parents]() {
                                                 if (has_parent) {
                                                     for (size_t par : parents) {
                                                         if (!this->cond_met[par]) {
                                                             this->cond_met[i] = 0;
                                                             return;
                                                         }
                                                     }
                                                 }
                                                 for (auto& cond : node->conditions) {
                                                     if (!cond->run({}, *exec_world).value_or(false)) {
                                                         this->cond_met[i] = 0;
                                                         return;
                                                     }
                                                 }
                                             })
                                             .name(std::format("{} [gate]", node_name(i)));
                    // Merge condition accesses for the gate
                    for (std::size_t ci = 0; ci < cn.node->conditions.size(); ci++) {
                        task_nodes[i].gate_access.extend(FilteredAccessSet(cn.node->condition_access[ci]));
                    }
                    task_nodes[i].in           = task_nodes[i].gate;
                    task_nodes[i].cond_decided = task_nodes[i].gate;
                } else {
                    // No gate needed — task is the entry point
                    task_nodes[i].cond_decided = tf::Task{};  // will be set to task below
                }

                // System or set task
                if (is_system) {
                    task_nodes[i].task =
                        taskflow
                            .emplace([this, i, node = cn.node, exclusive]() {
                                if (!this->cond_met[i]) return;
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
                            .name(node_name(i));
                    if (has_gate) {
                        task_nodes[i].gate.precede(task_nodes[i].task);
                    }
                } else {
                    // Set with children but no system — just use gate or a placeholder
                    if (!has_gate) {
                        task_nodes[i].task = taskflow.placeholder().name(node_name(i));
                    } else {
                        task_nodes[i].task = task_nodes[i].gate;  // gate IS the task
                    }
                }

                if (!has_gate) {
                    task_nodes[i].in           = task_nodes[i].task;
                    task_nodes[i].cond_decided = task_nodes[i].task;
                }

                // Done = join placeholder for children
                task_nodes[i].done = taskflow.placeholder().name(std::format("{} [done]", node_name(i)));
                if (is_system) {
                    task_nodes[i].task.precede(task_nodes[i].done);
                } else if (!has_gate) {
                    // placeholder set: done depends on task (which is the placeholder)
                    task_nodes[i].task.precede(task_nodes[i].done);
                } else {
                    // gate is the task, gate already set as in
                    task_nodes[i].gate.precede(task_nodes[i].done);
                }
                task_nodes[i].out = task_nodes[i].done;
            }
        }

        // Configure dependency edges between nodes.
        for (std::size_t i = 0; i < cache->nodes.size(); i++) {
            for (std::size_t dep : cache->nodes[i].depends) {
                task_nodes[dep].out.precede(task_nodes[i].in);
            }
            for (std::size_t par : cache->nodes[i].parents) {
                task_nodes[par].cond_decided.precede(task_nodes[i].in);
                task_nodes[i].out.precede(task_nodes[par].done);
            }
        }

        // --- Access conflict resolution ---
        std::vector<std::tuple<tf::Task, tf::Task, FilteredAccessSet*, bool>> task_accesses;
        std::unordered_map<tf::Task, std::optional<size_t>> task_to_node;
        for (std::size_t i = 0; i < cache->nodes.size(); i++) {
            auto& cn        = cache->nodes[i];
            auto& tn        = task_nodes[i];
            bool has_child  = !cn.children.empty();
            bool has_cond   = !cn.node->conditions.empty();
            bool has_parent = !cn.parents.empty();
            bool has_gate   = has_cond || has_parent;

            if (!has_child) {
                // Leaf: single merged task
                if (has_gate) {
                    // Use merged gate_access for the combined task
                    task_accesses.emplace_back(tn.task, tn.task, &tn.gate_access, tn.exclusive);
                    task_to_node[tn.task] = task_accesses.size() - 1;
                } else if (cn.node->system) {
                    task_accesses.emplace_back(tn.task, tn.task, &cn.node->system_access, tn.exclusive);
                    task_to_node[tn.task] = task_accesses.size() - 1;
                }
            } else {
                // Parent node: gate (if any) + system task (if any)
                if (has_gate && tn.gate != tn.task) {
                    // Separate gate and system task
                    task_accesses.emplace_back(tn.gate, tn.gate, &tn.gate_access, false);
                    task_to_node[tn.gate] = task_accesses.size() - 1;
                    if (cn.node->system) {
                        task_accesses.emplace_back(tn.task, tn.done, &cn.node->system_access, tn.exclusive);
                        task_to_node[tn.task] = task_accesses.size() - 1;
                    }
                } else if (has_gate) {
                    // gate IS the task (set with conditions, no system)
                    task_accesses.emplace_back(tn.gate, tn.done, &tn.gate_access, false);
                    task_to_node[tn.gate] = task_accesses.size() - 1;
                } else if (cn.node->system) {
                    task_accesses.emplace_back(tn.task, tn.done, &cn.node->system_access, tn.exclusive);
                    task_to_node[tn.task] = task_accesses.size() - 1;
                }
            }
        }
        taskflow.for_each_task([&](tf::Task t) {
            if (!task_to_node.contains(t)) {
                task_to_node[t] = std::nullopt;
            }
        });

        // Topological sort of task accesses.
        std::vector<std::size_t> access_order;
        access_order.reserve(task_accesses.size());
        std::unordered_set<tf::Task> visited;
        for (auto&& [task, index] : task_to_node) {
            if (task.num_predecessors() == 0) {
                if (index) access_order.push_back(*index);
                visited.insert(task);
            }
        }
        while (access_order.size() < task_accesses.size()) {
            bool progress = false;
            for (auto&& [task, index] : task_to_node) {
                if (visited.contains(task)) continue;
                bool ready = true;
                task.for_each_predecessor([&](tf::Task t) {
                    if (!visited.contains(t)) {
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

        // Compute transitive predecessors and add missing edges.
        std::unordered_map<tf::Task, std::unordered_set<tf::Task>> predecessors;
        auto get_predecessors = [&](this auto&& self, tf::Task t) -> const std::unordered_set<tf::Task>& {
            if (predecessors.contains(t)) {
                return predecessors[t];
            }
            auto& result = predecessors[t];
            t.for_each_predecessor([&](tf::Task pt) {
                result.insert(pt);
                auto&& pts = self(pt);
                result.insert(pts.begin(), pts.end());
            });
            return result;
        };
        taskflow.for_each_task([&](tf::Task t) { get_predecessors(t); });
        for (std::size_t i1 = 0; i1 < access_order.size(); i1++) {
            auto&& [task1, task1_done, access1, exclusive1] = task_accesses[access_order[i1]];
            for (std::size_t i2 = i1 + 1; i2 < access_order.size(); i2++) {
                auto&& [task2, task2_done, access2, exclusive2] = task_accesses[access_order[i2]];
                if (exclusive1 || exclusive2 || !access1->is_compatible(*access2)) {
                    if (!predecessors[task2].contains(task1)) {
                        task1_done.precede(task2);
                    }
                }
            }
        }

        // Cache has_system flag
        cached_has_system = false;
        for (auto& cn : cache->nodes) {
            if (cn.node->system) {
                cached_has_system = true;
                break;
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
    auto* cache_ptr = _data.cache.get();

    bool rebuild = m_impl->source.get() != cache_ptr || !m_impl->taskflow.has_value() ||
                   config.deferred != m_impl->exec_config.deferred;

    m_impl->exec_world  = &world;
    m_impl->exec_config = config;

    if (rebuild) {
        m_impl->build_taskflow(_data.cache);
        m_impl->source = _data.cache;
    }

    if (!m_impl->cached_has_system) return;

    std::fill(m_impl->cond_met.begin(), m_impl->cond_met.end(), 1);
    m_impl->executor->run(m_impl->taskflow.value()).wait();

    // End-of-iteration deferred handling.
    auto& nodes = _data.cache->nodes;
    switch (config.deferred) {
        case DeferredApply::ApplyEnd:
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].node->system && nodes[i].node->system->is_deferred()) {
                    nodes[i].node->system->apply_deferred(world);
                }
            }
            break;
        case DeferredApply::QueueDeferred:
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].node->system && nodes[i].node->system->is_deferred()) {
                    nodes[i].node->system->queue_deferred(world);
                }
            }
            break;
        case DeferredApply::ApplyDirect:
            break;
        case DeferredApply::Ignore: {
            std::vector<std::shared_ptr<Node>> to_apply;
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i].node->system && nodes[i].node->system->is_deferred()) {
                    to_apply.push_back(nodes[i].node);
                }
            }
            _data.pending_applies = std::move(to_apply);
        } break;
    }
}

}  // namespace epix::core

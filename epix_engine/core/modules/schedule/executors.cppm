module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <memory>
#include <vector>
#endif
export module epix.core:schedule.executors;

#ifdef EPIX_IMPORT_STD
import std;
#endif
import :schedule.schedule;

namespace epix::core::executors {
/** @brief Default executor using thread-pool-based parallel dispatch. */
export struct MultithreadClassicExecutor : ScheduleExecutor {
    void execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) override;
    meta::type_index type() const override { return meta::type_id<MultithreadClassicExecutor>(); }
};
/** @brief Single-thread executor that follows classic scheduling logic
 *  while running every condition/system on the caller thread. */
export struct SingleThreadExecutor : ScheduleExecutor {
    void execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) override;
    meta::type_index type() const override { return meta::type_id<SingleThreadExecutor>(); }
};
/** @brief A flat node in the expanded dependency graph used by FlatGraphExecutor.
 *  Each original node is split into a pre node (runs system/conditions) and a post node
 *  (waits for all children to finish). Hierarchy is encoded as pure dependency edges. */
struct FlatNode {
    std::size_t original_index;
    bool is_post;
    bool has_system;
    std::vector<std::size_t> flat_depends;
    std::vector<std::size_t> flat_successors;
    std::vector<std::size_t> parent_originals;  // pre nodes only: original parent indices for condition propagation
};
/** @brief Cached flat graph derived from ScheduleCache. */
struct FlatGraphCache {
    std::vector<FlatNode> nodes;          // 2*N entries: [2*i]=pre, [2*i+1]=post for original node i
    std::weak_ptr<ScheduleCache> source;  // detect when schedule cache is rebuilt
};
/** @brief Flat-graph executor that converts set hierarchy into pure dependency edges.
 *  Each set is split into pre/post nodes: children depend on the pre node and the
 *  post node depends on children's post nodes. This eliminates child_count tracking
 *  and simplifies the main execution loop to pure wait_count decrements. */
export struct MultithreadFlatExecutor : ScheduleExecutor {
    std::shared_ptr<FlatGraphCache> m_flat_cache;

    void rebuild_flat_cache(const std::shared_ptr<ScheduleCache>& cache);
    void execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) override;
    meta::type_index type() const override { return meta::type_id<MultithreadFlatExecutor>(); }
};
/** @brief Executor backed by taskflow's work-stealing scheduler.
 *  Converts the flat pre/post graph into a pure taskflow graph at cache-build time.
 *  Access conflicts between systems are resolved by inserting dependency edges so
 *  taskflow can fully manage parallelism. Conditions use taskflow's conditional tasking. */
export struct TaskflowExecutor : ScheduleExecutor {
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    TaskflowExecutor();
    explicit TaskflowExecutor(std::size_t num_threads);
    ~TaskflowExecutor() override;
    TaskflowExecutor(TaskflowExecutor&&) noexcept;
    TaskflowExecutor& operator=(TaskflowExecutor&&) noexcept;
    void execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) override;
    meta::type_index type() const override { return meta::type_id<TaskflowExecutor>(); }
};

export struct AutoExecutor : ScheduleExecutor {
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    AutoExecutor();
    ~AutoExecutor() override;
    AutoExecutor(AutoExecutor&&) noexcept;
    AutoExecutor& operator=(AutoExecutor&&) noexcept;
    void execute(ScheduleSystems& schedule, World& world, const ExecutorConfig& config) override;
    meta::type_index type() const override { return meta::type_id<AutoExecutor>(); }
};
}  // namespace epix::core::executors
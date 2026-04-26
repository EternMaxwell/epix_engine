module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <thread>
#endif
#include <spdlog/spdlog.h>

export module epix.core:app.task_pool_plugin;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.tasks;
import :app.decl;

namespace epix::core {

/**
 * @brief Controls how many threads are allocated to a single task pool.
 *
 * Mirrors `bevy_app::TaskPoolThreadAssignmentPolicy`.
 */
export struct TaskPoolThreadAssignmentPolicy {
    /** Minimum number of threads regardless of core count. */
    std::size_t min_threads = 1;
    /** Maximum number of threads regardless of core count. */
    std::size_t max_threads = 4;
    /**
     * Fraction of total logical cores to assign.
     * 1.0 means "all remaining" (used for the compute pool).
     */
    float percent = 0.25f;

    /** @brief Compute thread count given remaining and total logical cores. */
    [[nodiscard]] std::size_t get_num_threads(std::size_t remaining, std::size_t total) const {
        float proportion    = static_cast<float>(total) * percent;
        std::size_t desired = static_cast<std::size_t>(proportion);
        // round half-up
        if (proportion - static_cast<float>(desired) >= 0.5f) ++desired;
        desired = std::min(desired, remaining);
        return std::clamp(desired, min_threads, max_threads);
    }
};

/**
 * @brief Options for the three default task pools.
 *
 * Mirrors `bevy_app::TaskPoolOptions`. Defaults match Bevy exactly:
 *  - IO:           25 % of cores, min 1, max 4
 *  - AsyncCompute: 25 % of cores, min 1, max 4
 *  - Compute:      all remaining cores, min 1, no upper limit
 */
export struct TaskPoolOptions {
    std::size_t min_total_threads = 1;
    std::size_t max_total_threads = std::numeric_limits<std::size_t>::max();

    TaskPoolThreadAssignmentPolicy io            = {1, 4, 0.25f};
    TaskPoolThreadAssignmentPolicy async_compute = {1, 4, 0.25f};
    TaskPoolThreadAssignmentPolicy compute       = {1, std::numeric_limits<std::size_t>::max(), 1.0f};

    /** @brief Initialize all three global task pools according to this policy. */
    void create_default_pools() const {
        const std::size_t hw = std::thread::hardware_concurrency();
        const std::size_t total =
            std::clamp(hw == 0 ? 1u : static_cast<std::size_t>(hw), min_total_threads, max_total_threads);
        std::size_t remaining = total;

        const std::size_t io_threads = io.get_num_threads(remaining, total);
        remaining -= std::min(remaining, io_threads);
        spdlog::debug("[tasks] IO task pool: {} thread(s)", io_threads);
        tasks::IoTaskPool::get_or_init([io_threads] {
            // Use IoContext (asio::io_context) backend for the IO pool.
            // This is essential for correctness with ≥1 thread: all asset-loading
            // tasks are asio::awaitable<void> coroutines that use co_await.
            // With io_context, co_await suspends the coroutine and yields the
            // thread back to the event loop, allowing nested tasks posted to the
            // same pool to run — even with a single IO thread.  A ThreadPool
            // backend would also work for co_await-based code, but io_context
            // is the natural backend for async I/O coroutines and is consistent
            // with using asio throughout the engine.
            return tasks::TaskPoolBuilder{}
                .num_threads(io_threads)
                .thread_name("IO Task Pool")
                .backend(tasks::TaskPoolBackend::IoContext)
                .build();
        });

        const std::size_t async_threads = async_compute.get_num_threads(remaining, total);
        remaining -= std::min(remaining, async_threads);
        spdlog::debug("[tasks] AsyncCompute task pool: {} thread(s)", async_threads);
        tasks::AsyncComputeTaskPool::get_or_init([async_threads] {
            return tasks::TaskPoolBuilder{}.num_threads(async_threads).thread_name("Async Compute Task Pool").build();
        });

        const std::size_t compute_threads = compute.get_num_threads(remaining, total);
        spdlog::debug("[tasks] Compute task pool: {} thread(s)", compute_threads);
        tasks::ComputeTaskPool::get_or_init([compute_threads] {
            return tasks::TaskPoolBuilder{}.num_threads(compute_threads).thread_name("Compute Task Pool").build();
        });
    }
};

/**
 * @brief Initialises the three default task pools on `App::build()`.
 *
 * Mirrors `bevy_app::TaskPoolPlugin`. Add this plugin before any plugin
 * that uses IoTaskPool / AsyncComputeTaskPool / ComputeTaskPool (e.g. AssetPlugin).
 *
 * Thread counts follow `task_pool_options` (default = Bevy's policy):
 *  - IoTaskPool:           25 % of cores, clamped [1, 4]
 *  - AsyncComputeTaskPool: 25 % of cores, clamped [1, 4]
 *  - ComputeTaskPool:      remaining cores, min 1
 */
export struct TaskPoolPlugin {
    TaskPoolOptions task_pool_options;

    void build(App& /* app */) const { task_pool_options.create_default_pools(); }
};

}  // namespace epix::core

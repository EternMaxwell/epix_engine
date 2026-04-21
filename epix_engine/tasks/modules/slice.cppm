module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#endif

export module epix.tasks:slice;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :task_pool;

namespace epix::tasks {

/**
 * @brief Map a read-only slice in parallel chunks.
 * Matches `ParallelSlice::par_chunk_map`.
 */
export template <typename T, typename F, typename R = std::invoke_result_t<F, std::size_t, std::span<const T>>>
    requires std::invocable<F, std::size_t, std::span<const T>>
std::vector<R> par_chunk_map(std::span<const T> slice, TaskPool& pool, std::size_t chunk_size, F&& f) {
    std::vector<Task<R>> tasks;
    for (std::size_t i = 0; i < slice.size(); i += chunk_size) {
        std::size_t end          = std::min(i + chunk_size, slice.size());
        std::span<const T> chunk = slice.subspan(i, end - i);
        std::size_t index        = i / chunk_size;
        tasks.push_back(pool.spawn([chunk, index, fn = f]() mutable -> R { return fn(index, chunk); }));
    }
    std::vector<R> results;
    results.reserve(tasks.size());
    for (auto& t : tasks) {
        auto v = t.block();
        if (v) results.push_back(std::move(*v));
    }
    return results;
}

/**
 * @brief Map a read-only slice in parallel with automatic chunk sizing.
 * Matches `ParallelSlice::par_splat_map`.
 */
export template <typename T, typename F, typename R = std::invoke_result_t<F, std::size_t, std::span<const T>>>
    requires std::invocable<F, std::size_t, std::span<const T>>
std::vector<R> par_splat_map(std::span<const T> slice, TaskPool& pool, std::optional<std::size_t> max_tasks, F&& f) {
    std::size_t n = max_tasks.value_or(pool.thread_num());
    if (n == 0) n = 1;
    std::size_t chunk_size = (slice.size() + n - 1) / n;
    if (chunk_size == 0) chunk_size = 1;
    return par_chunk_map(slice, pool, chunk_size, std::forward<F>(f));
}

/**
 * @brief Map a mutable slice in parallel chunks.
 * Matches `ParallelSliceMut::par_chunk_map_mut`.
 */
export template <typename T, typename F, typename R = std::invoke_result_t<F, std::size_t, std::span<T>>>
    requires std::invocable<F, std::size_t, std::span<T>>
std::vector<R> par_chunk_map_mut(std::span<T> slice, TaskPool& pool, std::size_t chunk_size, F&& f) {
    std::vector<Task<R>> tasks;
    for (std::size_t i = 0; i < slice.size(); i += chunk_size) {
        std::size_t end    = std::min(i + chunk_size, slice.size());
        std::span<T> chunk = slice.subspan(i, end - i);
        std::size_t index  = i / chunk_size;
        tasks.push_back(pool.spawn([chunk, index, fn = f]() mutable -> R { return fn(index, chunk); }));
    }
    std::vector<R> results;
    results.reserve(tasks.size());
    for (auto& t : tasks) {
        auto v = t.block();
        if (v) results.push_back(std::move(*v));
    }
    return results;
}

/**
 * @brief Map a mutable slice in parallel with automatic chunk sizing.
 * Matches `ParallelSliceMut::par_splat_map_mut`.
 */
export template <typename T, typename F, typename R = std::invoke_result_t<F, std::size_t, std::span<T>>>
    requires std::invocable<F, std::size_t, std::span<T>>
std::vector<R> par_splat_map_mut(std::span<T> slice, TaskPool& pool, std::optional<std::size_t> max_tasks, F&& f) {
    std::size_t n = max_tasks.value_or(pool.thread_num());
    if (n == 0) n = 1;
    std::size_t chunk_size = (slice.size() + n - 1) / n;
    if (chunk_size == 0) chunk_size = 1;
    return par_chunk_map_mut(slice, pool, chunk_size, std::forward<F>(f));
}

}  // namespace epix::tasks

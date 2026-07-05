#pragma once

// ── C++23 adaptation of Bevy's slice.rs ───────────────────────────────────
//
// Reference: https://github.com/bevyengine/bevy/blob/main/crates/bevy_tasks/src/slice.rs
//
// Provides free functions for parallel mapping over spans/ranges:
//   par_chunk_map(pool, slice, chunk_size, fn)  → Vec<R>
//   par_splat_map(pool, slice, max_tasks, fn)   → Vec<R>
//
// Matches Bevy's ParallelSlice / ParallelSliceMut traits, but as free
// functions since C++ doesn't have extension traits.

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <epix/common.hpp>
#include <span>
#include <vector>
#endif

#include <epix/task/task_pool.hpp>

namespace epix::tasks {

// ── par_chunk_map ─────────────────────────────────────────────────────────

/**
 * @brief Split a span into chunks of at most `chunk_size` elements and
 *        map each chunk in parallel across the given TaskPool.
 *
 * Returns results in the same order as the input chunks.
 *
 * Matches Bevy's `ParallelSlice::par_chunk_map`.
 *
 * @param pool       The TaskPool to schedule work on.
 * @param data       Input span to process.
 * @param chunk_size Maximum elements per chunk.
 * @param fn         Mapping function: (size_t chunk_index, std::span<const T> chunk) -> R.
 * @return Vector of mapped results in chunk order.
 */
EPIX_EXPORT template <typename T, typename F, typename R = std::invoke_result_t<F, size_t, std::span<const T>>>
    requires std::invocable<F, size_t, std::span<const T>>
[[nodiscard]] inline std::vector<R> par_chunk_map(TaskPool& pool, std::span<const T> data, size_t chunk_size, F fn) {
    if (data.empty()) return {};

    // Pre-calculate chunks so we know the result count.
    struct Chunk {
        size_t index;
        size_t offset;
        size_t length;
    };
    std::vector<Chunk> chunks;
    for (size_t off = 0; off < data.size(); off += chunk_size) {
        size_t len = std::min(chunk_size, data.size() - off);
        chunks.push_back({chunks.size(), off, len});
    }

    return pool.scope<R>([&](Scope<R>& s) {
        for (const auto& c : chunks) {
            auto sub = data.subspan(c.offset, c.length);
            s.spawn([=, &fn]() -> R { return fn(c.index, sub); });
        }
    });
}

// ── par_splat_map ─────────────────────────────────────────────────────────

/**
 * @brief Split a span across the pool threads evenly and map each chunk
 *        in parallel.
 *
 * If `max_tasks` is provided, it caps the number of chunks.
 * Otherwise, one chunk per pool thread is used.
 *
 * Matches Bevy's `ParallelSlice::par_splat_map`.
 *
 * @param pool       The TaskPool to schedule work on.
 * @param data       Input span to process.
 * @param max_tasks  Optional cap on the number of parallel tasks.
 * @param fn         Mapping function: (size_t chunk_index, std::span<const T> chunk) -> R.
 * @return Vector of mapped results in chunk order.
 */
EPIX_EXPORT template <typename T, typename F, typename R = std::invoke_result_t<F, size_t, std::span<const T>>>
    requires std::invocable<F, size_t, std::span<const T>>
[[nodiscard]] inline std::vector<R> par_splat_map(TaskPool& pool,
                                                  std::span<const T> data,
                                                  std::optional<size_t> max_tasks,
                                                  F fn) {
    if (data.empty()) return {};

    size_t num_threads = pool.thread_num();
    size_t max         = max_tasks.value_or(size_t(-1));
    size_t chunk_size  = std::max<size_t>(1, std::max(data.size() / num_threads, data.size() / max));

    return par_chunk_map(pool, data, chunk_size, std::move(fn));
}

}  // namespace epix::tasks

#pragma once

// ── C++23 ranges-friendly parallel processing ─────────────────────────────
//
// Inspired by Bevy's ParallelIterator but redesigned for C++ ranges.
// par_range wraps a contiguous range (zero-copy via std::span) and
// dispatches chunked work across a TaskPool.
//
//   make_par(vec)           — non-owning view of a contiguous range
//   make_par(span)          — view of a span
//
// Operations (all take TaskPool&, execute eagerly):
//   .for_each(pool, fn)             — mutable / const, chunked parallel
//   .transform<U>(pool, fn)         — map → vector<U>, order-preserving
//   .filter(pool, pred)             — filter → vector<T>, order-preserving
//   .reduce(pool, init, op)         — parallel reduce
//   .fold(pool, init, op)           — parallel fold
//   .any(pool, pred) / .all(pool, pred)
//   .sum(pool) / .product(pool)     — arithmetic reductions
//   .collect<C>()                   — copy into container C

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <epix/common.hpp>
#include <functional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#endif

#include <epix/task/task_pool.hpp>

namespace epix::task {

// ── Internal helpers ──────────────────────────────────────────────────────

namespace internal {

inline size_t chunk_size_for(size_t total, size_t num_threads, std::optional<size_t> max_tasks) {
    size_t max = max_tasks.value_or(size_t(-1));
    return std::max<size_t>(1, std::max(total / num_threads, total / max));
}

}  // namespace internal

// ── par_range ─────────────────────────────────────────────────────────────

EPIX_EXPORT template <typename T>
struct par_range {
    using value_type = T;

    // ── Constructors (zero-copy views) ────────────────────────────────

    explicit par_range(std::span<T> s) : m_data(s) {}

    template <std::ranges::contiguous_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, T>
    explicit par_range(R& rng) : m_data(std::ranges::data(rng), std::ranges::size(rng)) {}

    template <std::ranges::contiguous_range R>
        requires std::convertible_to<std::ranges::range_value_t<R>, T>
    explicit par_range(const R& rng) = delete;  // avoid dangling: pass by non-const ref

    [[nodiscard]] size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }

    // ── for_each ──────────────────────────────────────────────────────

    void for_each(TaskPool& pool, std::invocable<T&> auto fn) {
        if (m_data.empty()) return;
        size_t cs = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        par_chunk_map_void(pool, cs, [&fn](size_t, std::span<T> chunk) {
            for (auto& elem : chunk) {
                fn(elem);
            }
        });
    }

    void for_each(TaskPool& pool, std::invocable<const T&> auto fn) const {
        const_cast<par_range*>(this)->par_chunk_map_void(
            pool, internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt),
            [&fn](size_t, std::span<T> chunk) {
                for (const auto& elem : chunk) {
                    fn(elem);
                }
            });
    }

    // ── transform ─────────────────────────────────────────────────────

    template <typename U, std::invocable<const T&> F>
        requires std::convertible_to<std::invoke_result_t<F, const T&>, U>
    [[nodiscard]] std::vector<U> transform(TaskPool& pool, F fn) const {
        if (m_data.empty()) return {};
        size_t cs   = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        auto chunks = const_cast<par_range*>(this)->template par_chunk_map<std::vector<U>>(
            pool, cs, [&fn](size_t, std::span<T> chunk) {
                std::vector<U> out;
                out.reserve(chunk.size());
                for (const auto& elem : chunk) out.push_back(fn(elem));
                return out;
            });
        std::vector<U> result;
        result.reserve(m_data.size());
        for (auto& c : chunks)
            result.insert(result.end(), std::make_move_iterator(c.begin()), std::make_move_iterator(c.end()));
        return result;
    }

    // ── filter ────────────────────────────────────────────────────────

    [[nodiscard]] std::vector<T> filter(TaskPool& pool, std::predicate<const T&> auto pred) const {
        if (m_data.empty()) return {};
        size_t cs   = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        auto chunks = const_cast<par_range*>(this)->template par_chunk_map<std::vector<T>>(
            pool, cs, [&pred](size_t, std::span<T> chunk) {
                std::vector<T> out;
                for (const auto& elem : chunk) {
                    if (pred(elem)) out.push_back(elem);
                }
                return out;
            });
        std::vector<T> result;
        for (auto& c : chunks) {
            result.insert(result.end(), std::make_move_iterator(c.begin()), std::make_move_iterator(c.end()));
        }
        return result;
    }

    // ── reduce ────────────────────────────────────────────────────────

    [[nodiscard]] T reduce(TaskPool& pool, T init, std::regular_invocable<T, T> auto op) const
        requires std::convertible_to<std::invoke_result_t<decltype(op), T, T>, T>
    {
        if (m_data.empty()) return init;
        if (m_data.size() == 1) return op(init, m_data[0]);
        size_t cs = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        auto partials =
            const_cast<par_range*>(this)->template par_chunk_map<T>(pool, cs, [&op](size_t, std::span<T> chunk) {
                T acc = chunk[0];
                for (size_t i = 1; i < chunk.size(); ++i) {
                    acc = op(acc, chunk[i]);
                }
                return acc;
            });
        T result = init;
        for (auto& p : partials) {
            result = op(std::move(result), std::move(p));
        }
        return result;
    }

    // ── fold ──────────────────────────────────────────────────────────

    [[nodiscard]] T fold(TaskPool& pool, T init, std::regular_invocable<T, T> auto op) const
        requires std::convertible_to<std::invoke_result_t<decltype(op), T, T>, T>
    {
        if (m_data.empty()) return init;
        size_t cs = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        auto partials =
            const_cast<par_range*>(this)->template par_chunk_map<T>(pool, cs, [&op, &init](size_t, std::span<T> chunk) {
                T acc = init;
                for (const auto& elem : chunk) {
                    acc = op(acc, elem);
                }
                return acc;
            });
        T result = init;
        for (auto& p : partials) {
            result = op(std::move(result), std::move(p));
        }
        return result;
    }

    // ── any / all ─────────────────────────────────────────────────────

    [[nodiscard]] bool any(TaskPool& pool, std::predicate<const T&> auto pred) const {
        if (m_data.empty()) return false;
        size_t cs    = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        auto results = const_cast<par_range*>(this)->template par_chunk_map<bool>(
            pool, cs, [&pred](size_t, std::span<T> chunk) { return std::ranges::any_of(chunk, pred); });
        return std::ranges::any_of(results, std::identity{});
    }

    [[nodiscard]] bool all(TaskPool& pool, std::predicate<const T&> auto pred) const {
        if (m_data.empty()) return true;
        size_t cs    = internal::chunk_size_for(m_data.size(), pool.thread_num(), std::nullopt);
        auto results = const_cast<par_range*>(this)->template par_chunk_map<bool>(
            pool, cs, [&pred](size_t, std::span<T> chunk) { return std::ranges::all_of(chunk, pred); });
        return std::ranges::all_of(results, std::identity{});
    }

    // ── sum / product ─────────────────────────────────────────────────

    [[nodiscard]] T sum(TaskPool& pool) const
        requires std::is_arithmetic_v<T>
    {
        return reduce(pool, T{0}, std::plus{});
    }

    [[nodiscard]] T product(TaskPool& pool) const
        requires std::is_arithmetic_v<T>
    {
        return reduce(pool, T{1}, std::multiplies{});
    }

    // ── collect ───────────────────────────────────────────────────────

    template <typename C>
    [[nodiscard]] C collect() const {
        return C(m_data.begin(), m_data.end());
    }

    // ── range interface ───────────────────────────────────────────────

    [[nodiscard]] auto begin() { return m_data.begin(); }
    [[nodiscard]] auto end() { return m_data.end(); }
    [[nodiscard]] auto begin() const { return m_data.begin(); }
    [[nodiscard]] auto end() const { return m_data.end(); }

   private:
    std::span<T> m_data;

    template <typename R, typename F>
        requires std::invocable<F, size_t, std::span<T>>
    [[nodiscard]] std::vector<R> par_chunk_map(TaskPool& pool, size_t chunk_size, F fn) {
        struct Chunk {
            size_t index;
            size_t offset;
            size_t length;
        };
        std::vector<Chunk> chunks;
        for (size_t off = 0; off < m_data.size(); off += chunk_size) {
            chunks.push_back({chunks.size(), off, std::min(chunk_size, m_data.size() - off)});
        }

        std::vector<std::optional<R>> slots(chunks.size());
        pool.scope<int>([&](Scope<int>& s) {
            for (const auto& c : chunks) {
                s.spawn([this, &fn, &slots, c]() -> int {
                    slots[c.index] = fn(c.index, std::span<T>(m_data.data() + c.offset, c.length));
                    return 0;
                });
            }
        });

        std::vector<R> results;
        results.reserve(chunks.size());
        for (auto& opt : slots) {
            results.push_back(std::move(*opt));
        }
        return results;
    }

    void par_chunk_map_void(TaskPool& pool, size_t chunk_size, std::invocable<size_t, std::span<T>> auto fn) {
        struct Chunk {
            size_t offset;
            size_t length;
        };
        std::vector<Chunk> chunks;
        for (size_t off = 0; off < m_data.size(); off += chunk_size) {
            chunks.push_back({off, std::min(chunk_size, m_data.size() - off)});
        }
        pool.scope<int>([&](Scope<int>& s) {
            for (size_t i = 0; i < chunks.size(); ++i) {
                s.spawn([this, i, &fn, &chunks]() -> int {
                    auto& c = chunks[i];
                    fn(i, std::span<T>(m_data.data() + c.offset, c.length));
                    return 0;
                });
            }
        });
    }
};

// ── Deduction guide ───────────────────────────────────────────────────────

template <typename T>
par_range(std::span<T>) -> par_range<T>;

// ── Factory ───────────────────────────────────────────────────────────────

EPIX_EXPORT template <typename T>
[[nodiscard]] par_range<T> make_par(std::span<T> s) {
    return par_range<T>(s);
}

EPIX_EXPORT template <std::ranges::contiguous_range R>
[[nodiscard]] auto make_par(R& rng) {
    return par_range<std::ranges::range_value_t<R>>(rng);
}

}  // namespace epix::task

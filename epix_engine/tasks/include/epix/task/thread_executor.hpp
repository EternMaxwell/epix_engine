#pragma once

// ── C++23 adaptation of Bevy's thread_executor.rs ─────────────────────────
//
// Reference: https://github.com/bevyengine/bevy/blob/main/crates/bevy_tasks/src/thread_executor.rs
//
// Provides:
//   ThreadExecutor        — single-thread executor, only tickable on owning thread
//   ThreadExecutorTicker  — borrows the executor for manual ticking
//
// Matches Bevy's API:
//   ThreadExecutor::new()
//   exec.spawn(future)           → Task<T>
//   exec.ticker()                → optional<ThreadExecutorTicker> (None if wrong thread)
//   ticker.tick()                (async)
//   ticker.try_tick()            (sync, bool)
//   exec.is_same(&other)         (pointer equality)

#ifndef EPIX_CXX_MODULE
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <chrono>
#include <epix/async_task.hpp>
#include <epix/common.hpp>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#endif

namespace epix::tasks {

// ── Forward declarations ──────────────────────────────────────────────────

EPIX_EXPORT struct ThreadExecutor;

// ── ThreadExecutorTicker ──────────────────────────────────────────────────

/** @brief RAII ticker that drives the ThreadExecutor. */
EPIX_EXPORT struct ThreadExecutorTicker {
    /** @brief Run one scheduled task (blocking poll). */
    void tick();

    /** @brief Try to run one task synchronously. Returns true if executed. */
    [[nodiscard]] bool try_tick();

   private:
    friend struct ThreadExecutor;
    explicit ThreadExecutorTicker(ThreadExecutor& exec) noexcept : m_exec(&exec) {}
    ThreadExecutor* m_exec;
};

// ── ThreadExecutor ────────────────────────────────────────────────────────

/**
 * @brief An executor that can only be ticked on the thread it was created on,
 *        but allows spawning Send tasks from any thread.
 *
 * Matches Bevy's `ThreadExecutor<'task>`:
 *   - spawn(future) → Task<T>   (any thread)
 *   - ticker() → optional<ThreadExecutorTicker>  (only on owning thread)
 *   - is_same(&other) → bool
 */
EPIX_EXPORT struct ThreadExecutor {
    ThreadExecutor();
    ~ThreadExecutor();

    ThreadExecutor(const ThreadExecutor&)            = delete;
    ThreadExecutor& operator=(const ThreadExecutor&) = delete;
    ThreadExecutor(ThreadExecutor&&)                 = delete;
    ThreadExecutor& operator=(ThreadExecutor&&)      = delete;

    /** @brief Spawn work onto this executor. Callable from any thread. */
    template <typename F>
        requires std::invocable<F> && std::move_constructible<F>
    [[nodiscard]] auto spawn(F&& work) -> async_task::Task<std::invoke_result_t<F>>;

    /** @brief Get a ticker. Returns nullopt if called from a different thread. */
    [[nodiscard]] std::optional<ThreadExecutorTicker> ticker();

    /** @brief Returns true if `this` and `other` are the same executor. */
    [[nodiscard]] bool is_same(const ThreadExecutor& other) const noexcept { return this == &other; }

   private:
    friend struct ThreadExecutorTicker;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── Implementation ────────────────────────────────────────────────────────

struct ThreadExecutor::Impl {
    asio::io_context ctx;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard;
    std::thread::id owner_id;

    Impl() : work_guard(asio::make_work_guard(ctx)), owner_id(std::this_thread::get_id()) {}
};

inline ThreadExecutor::ThreadExecutor() : m_impl(std::make_unique<Impl>()) {}

inline ThreadExecutor::~ThreadExecutor() {
    m_impl->work_guard.reset();
    m_impl->ctx.stop();
}

template <typename F>
    requires std::invocable<F> && std::move_constructible<F>
inline auto ThreadExecutor::spawn(F&& work) -> async_task::Task<std::invoke_result_t<F>> {
    using T = std::invoke_result_t<F>;

    auto [runnable, task] = async_task::spawn(std::forward<F>(work),
                                              [ctx = &m_impl->ctx](async_task::Runnable r, async_task::ScheduleInfo) {
                                                  asio::post(*ctx, [r = std::move(r)]() mutable { r.run(); });
                                              });

    runnable.schedule();
    return std::move(task);
}

inline std::optional<ThreadExecutorTicker> ThreadExecutor::ticker() {
    if (std::this_thread::get_id() != m_impl->owner_id) {
        return std::nullopt;
    }
    return ThreadExecutorTicker{*this};
}

inline void ThreadExecutorTicker::tick() { m_exec->m_impl->ctx.poll_one(); }

inline bool ThreadExecutorTicker::try_tick() { return m_exec->m_impl->ctx.poll_one() > 0; }

}  // namespace epix::tasks

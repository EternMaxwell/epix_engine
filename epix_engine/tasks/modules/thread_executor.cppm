module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <exception>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#endif
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/thread_pool.hpp>

export module epix.tasks:thread_executor;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :task;

namespace epix::tasks {

export struct ThreadExecutorTicker;

/**
 * @brief An executor that can only be ticked on the thread it was created on.
 * Tasks can be spawned from any thread, but ticking must happen on the owning thread.
 * Matches `bevy_tasks::ThreadExecutor`.
 */
export struct ThreadExecutor {
   private:
    asio::thread_pool m_ctx{1};
    std::thread::id m_thread_id;

    friend struct ThreadExecutorTicker;

   public:
    ThreadExecutor() : m_thread_id(std::this_thread::get_id()) {}

    ThreadExecutor(const ThreadExecutor&)            = delete;
    ThreadExecutor& operator=(const ThreadExecutor&) = delete;

    /**
     * @brief Spawn a callable on this single-thread executor.
     * Returns a Task<T>. Matches `ThreadExecutor::spawn`.
     */
    template <typename F>
        requires std::invocable<F> && (!std::is_void_v<std::invoke_result_t<F>>)
    auto spawn(F&& f) -> Task<std::invoke_result_t<F>> {
        using T            = std::invoke_result_t<F>;
        auto [task, state] = Task<T>::make();
        asio::co_spawn(
            m_ctx.get_executor(),
            [s = std::move(state), fn = std::forward<F>(f)]() mutable -> asio::awaitable<void> {
                try {
                    s->set_value(fn());
                } catch (...) {
                    s->set_exception(std::current_exception());
                }
                co_return;
            },
            asio::detached);
        return std::move(task);
    }

    template <typename F>
        requires std::invocable<F> && std::is_void_v<std::invoke_result_t<F>>
    Task<void> spawn(F&& f) {
        auto [task, state] = Task<void>::make();
        asio::co_spawn(
            m_ctx.get_executor(),
            [s = std::move(state), fn = std::forward<F>(f)]() mutable -> asio::awaitable<void> {
                try {
                    fn();
                    s->set_done();
                } catch (...) {
                    s->set_exception(std::current_exception());
                }
                co_return;
            },
            asio::detached);
        return std::move(task);
    }

    /**
     * @brief Returns true if other is the same executor object.
     * Matches `ThreadExecutor::is_same`.
     */
    bool is_same(const ThreadExecutor& other) const { return this == &other; }

    /**
     * @brief Returns a ticker only if called from the owning thread.
     * Matches `ThreadExecutor::ticker() -> Option<ThreadExecutorTicker>`.
     */
    std::optional<ThreadExecutorTicker> ticker();
};

/**
 * @brief Used to tick a ThreadExecutor.
 * Matches `bevy_tasks::ThreadExecutorTicker`.
 */
export struct ThreadExecutorTicker {
   private:
    ThreadExecutor* m_executor;

   public:
    explicit ThreadExecutorTicker(ThreadExecutor* exec) : m_executor(exec) {}
    ThreadExecutorTicker(const ThreadExecutorTicker&)            = delete;
    ThreadExecutorTicker& operator=(const ThreadExecutorTicker&) = delete;
    ThreadExecutorTicker(ThreadExecutorTicker&&)                 = default;
    ThreadExecutorTicker& operator=(ThreadExecutorTicker&&)      = default;

    /**
     * @brief Try to tick one task. Returns false if no task is ready.
     * Matches `ThreadExecutorTicker::try_tick`.
     */
    bool try_tick() {
        // single_thread_context runs on its own thread, so we can't "tick" from outside.
        // This is a no-op placeholder that returns false.
        return false;
    }
};

inline std::optional<ThreadExecutorTicker> ThreadExecutor::ticker() {
    if (std::this_thread::get_id() == m_thread_id) {
        return ThreadExecutorTicker{this};
    }
    return std::nullopt;
}

}  // namespace epix::tasks

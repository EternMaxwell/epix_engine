module;

#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/post.hpp>
#include <coroutine>

export module epix.tasks:task;

import std;

namespace epix::tasks {

// ─── Internal shared state ────────────────────────────────────────────────────

namespace detail {

/**
 * Shared state for Task<T>.  Completed on the pool thread; awaited on any
 * coroutine or blocking thread.
 */
template <typename T>
struct TaskState {
    std::optional<T> value;
    std::exception_ptr exception;
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    // Guarded by mtx:
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::move_only_function<void()>> waiters;

    void set_value(T v) {
        std::vector<std::move_only_function<void()>> to_resume;
        {
            std::unique_lock lock(mtx);
            value = std::move(v);
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto& f : to_resume) f();
    }

    void set_exception(std::exception_ptr e) {
        std::vector<std::move_only_function<void()>> to_resume;
        {
            std::unique_lock lock(mtx);
            exception = e;
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto& f : to_resume) f();
    }
};

template <>
struct TaskState<void> {
    std::exception_ptr exception;
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::move_only_function<void()>> waiters;

    void set_done() {
        std::vector<std::move_only_function<void()>> to_resume;
        {
            std::unique_lock lock(mtx);
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto& f : to_resume) f();
    }

    void set_exception(std::exception_ptr e) {
        std::vector<std::move_only_function<void()>> to_resume;
        {
            std::unique_lock lock(mtx);
            exception = e;
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto& f : to_resume) f();
    }
};

}  // namespace detail

// ─── Task<T> ─────────────────────────────────────────────────────────────────

/**
 * @brief A spawned, awaitable task handle.
 *
 * Eagerly started at spawn time; the result is stored in a shared state.
 * Supports:
 *   - `co_await std::move(task)` inside any C++20 coroutine
 *   - `task.detach()` — drop the handle, work keeps running
 *   - `task.is_finished()` — non-blocking poll
 *   - `task.block()` — blocking wait (for non-coroutine contexts)
 *
 * API matches `bevy_tasks::Task<T>` (`impl Future for Task<T>`).
 */
export template <typename T>
struct Task {
   private:
    std::shared_ptr<detail::TaskState<T>> m_state;

   public:
    Task() = default;
    explicit Task(std::shared_ptr<detail::TaskState<T>> s) : m_state(std::move(s)) {}
    Task(Task&&)                 = default;
    Task& operator=(Task&&)      = default;
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    /** Detach: drop the handle; work keeps running in the background. */
    void detach() { m_state.reset(); }

    /** Non-blocking: returns true if the work has completed. */
    bool is_finished() const { return !m_state || m_state->done.load(std::memory_order_acquire); }

    /**
     * @brief Cancel the task and wait for it to stop running.
     * Returns the task's output if it was completed just before it got canceled,
     * or nullopt if it didn't complete.
     * Matches `bevy_tasks::Task::cancel`.
     */
    std::optional<T> cancel() {
        if (!m_state) return std::nullopt;
        m_state->cancelled.store(true, std::memory_order_release);
        // Wait for completion (the posted handler will still run but check cancelled)
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [&] { return m_state->done.load(std::memory_order_acquire); });
        }
        if (m_state->exception) return std::nullopt;
        return std::move(m_state->value);
    }

    /**
     * @brief Blocking wait. Returns the value, or nullopt if handle was detached.
     * Use in non-coroutine contexts only.
     */
    std::optional<T> block() {
        if (!m_state) return std::nullopt;
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [&] { return m_state->done.load(std::memory_order_acquire); });
        }
        if (m_state->exception) std::rethrow_exception(m_state->exception);
        return std::move(m_state->value);
    }

    explicit operator bool() const { return m_state && !m_state->done.load(); }

    /**
     * @brief Make Task<T> usable as `co_await task` inside `asio::awaitable<T>`.
     * Calling this consumes the Task (moves m_state). Completion is always
     * posted back to the caller's associated executor so asio's strand model
     * is respected.
     * Requires T to be default-constructible (asio passes a dummy T{} when
     * propagating an exception through the void(exception_ptr,T) signature).
     */
    template <ASIO_COMPLETION_TOKEN_FOR(void(std::exception_ptr, T)) CompletionToken>
    auto operator()(CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(std::exception_ptr, T)>(
            [state = std::move(m_state)](auto handler) mutable {
                auto ex = asio::get_associated_executor(handler);

                auto invoke = [state, h = std::move(handler)]() mutable {
                    if (state->exception)
                        h(state->exception, T{});
                    else
                        h(nullptr, std::move(*state->value));
                };

                {
                    std::unique_lock lock(state->mtx);
                    if (!state->done.load(std::memory_order_acquire)) {
                        state->waiters.push_back(
                            [invoke = std::move(invoke), ex]() mutable { asio::post(ex, std::move(invoke)); });
                        return;
                    }
                }
                asio::post(ex, std::move(invoke));
            },
            token);
    }

    /** Factory: create a Task with a fresh, unfulfilled shared state. */
    static std::pair<Task, std::shared_ptr<detail::TaskState<T>>> make() {
        auto state = std::make_shared<detail::TaskState<T>>();
        return {Task{state}, state};
    }
};

/**
 * @brief Void specialisation — no value, just completion/exception.
 */
export template <>
struct Task<void> {
   private:
    std::shared_ptr<detail::TaskState<void>> m_state;

   public:
    Task() = default;
    explicit Task(std::shared_ptr<detail::TaskState<void>> s) : m_state(std::move(s)) {}
    Task(Task&&)                 = default;
    Task& operator=(Task&&)      = default;
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    void detach() { m_state.reset(); }

    bool is_finished() const { return !m_state || m_state->done.load(std::memory_order_acquire); }

    /**
     * @brief Cancel the task and wait for it to stop running.
     * Matches `bevy_tasks::Task::cancel` (void specialisation).
     */
    void cancel() {
        if (!m_state) return;
        m_state->cancelled.store(true, std::memory_order_release);
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [&] { return m_state->done.load(std::memory_order_acquire); });
        }
    }

    void block() {
        if (!m_state) return;
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [&] { return m_state->done.load(std::memory_order_acquire); });
        }
        if (m_state->exception) std::rethrow_exception(m_state->exception);
    }

    explicit operator bool() const { return m_state && !m_state->done.load(); }

    /** @brief Make Task<void> usable as `co_await task` inside `asio::awaitable<void>`. */
    template <ASIO_COMPLETION_TOKEN_FOR(void(std::exception_ptr)) CompletionToken>
    auto operator()(CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(std::exception_ptr)>(
            [state = std::move(m_state)](auto handler) mutable {
                auto ex = asio::get_associated_executor(handler);

                auto invoke = [state, h = std::move(handler)]() mutable { h(state ? state->exception : nullptr); };

                if (state) {
                    std::unique_lock lock(state->mtx);
                    if (!state->done.load(std::memory_order_acquire)) {
                        state->waiters.push_back(
                            [invoke = std::move(invoke), ex]() mutable { asio::post(ex, std::move(invoke)); });
                        return;
                    }
                }
                asio::post(ex, std::move(invoke));
            },
            token);
    }

    static std::pair<Task, std::shared_ptr<detail::TaskState<void>>> make() {
        auto state = std::make_shared<detail::TaskState<void>>();
        return {Task{state}, state};
    }
};

}  // namespace epix::tasks

module;

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
    // Guarded by mtx:
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::coroutine_handle<>> waiters;

    void set_value(T v) {
        std::vector<std::coroutine_handle<>> to_resume;
        {
            std::unique_lock lock(mtx);
            value = std::move(v);
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto h : to_resume) h.resume();
    }

    void set_exception(std::exception_ptr e) {
        std::vector<std::coroutine_handle<>> to_resume;
        {
            std::unique_lock lock(mtx);
            exception = e;
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto h : to_resume) h.resume();
    }
};

template <>
struct TaskState<void> {
    std::exception_ptr exception;
    std::atomic<bool> done{false};
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::coroutine_handle<>> waiters;

    void set_done() {
        std::vector<std::coroutine_handle<>> to_resume;
        {
            std::unique_lock lock(mtx);
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto h : to_resume) h.resume();
    }

    void set_exception(std::exception_ptr e) {
        std::vector<std::coroutine_handle<>> to_resume;
        {
            std::unique_lock lock(mtx);
            exception = e;
            done.store(true, std::memory_order_release);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto h : to_resume) h.resume();
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

    struct Awaiter {
        std::shared_ptr<detail::TaskState<T>> state;

        bool await_ready() noexcept { return !state || state->done.load(std::memory_order_acquire); }

        // Returns false (don't suspend) if already done; true (suspend) otherwise.
        bool await_suspend(std::coroutine_handle<> h) {
            std::unique_lock lock(state->mtx);
            if (state->done.load(std::memory_order_acquire)) return false;
            state->waiters.push_back(h);
            return true;
        }

        T await_resume() {
            if (state->exception) std::rethrow_exception(state->exception);
            return std::move(*state->value);
        }
    };

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
     * @brief Make co_await-able in any C++20 coroutine.
     * Suspends until the task completes, then returns the value.
     */
    Awaiter operator co_await() && noexcept { return Awaiter{std::move(m_state)}; }

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

    struct Awaiter {
        std::shared_ptr<detail::TaskState<void>> state;

        bool await_ready() noexcept { return !state || state->done.load(std::memory_order_acquire); }

        bool await_suspend(std::coroutine_handle<> h) {
            std::unique_lock lock(state->mtx);
            if (state->done.load(std::memory_order_acquire)) return false;
            state->waiters.push_back(h);
            return true;
        }

        void await_resume() {
            if (state && state->exception) std::rethrow_exception(state->exception);
        }
    };

   public:
    Task() = default;
    explicit Task(std::shared_ptr<detail::TaskState<void>> s) : m_state(std::move(s)) {}
    Task(Task&&)                 = default;
    Task& operator=(Task&&)      = default;
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    void detach() { m_state.reset(); }

    bool is_finished() const { return !m_state || m_state->done.load(std::memory_order_acquire); }

    void block() {
        if (!m_state) return;
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [&] { return m_state->done.load(std::memory_order_acquire); });
        }
        if (m_state->exception) std::rethrow_exception(m_state->exception);
    }

    explicit operator bool() const { return m_state && !m_state->done.load(); }

    Awaiter operator co_await() && noexcept { return Awaiter{std::move(m_state)}; }

    static std::pair<Task, std::shared_ptr<detail::TaskState<void>>> make() {
        auto state = std::make_shared<detail::TaskState<void>>();
        return {Task{state}, state};
    }
};

}  // namespace epix::tasks

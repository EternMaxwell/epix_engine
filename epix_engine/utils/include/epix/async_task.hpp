#pragma once

// ── C++23 adaptation of Rust's async-task crate (v4.7.1) ──────────────────
//
// Reference: https://github.com/smol-rs/async-task
//
// Matches the API and behaviour:
//   spawn(work, schedule) → (Runnable, Task<T>)
//
//   Runnable           type-erased, executor-side: run(), schedule(), waker()
//   Task<T>            #[must_use], typed awaitable: cancel(), fallible(), detach(), is_finished()
//   FallibleTask<T>    like Task but co_await → optional<T> (None on cancel/fail)
//   Waker              copyable: wake(), wake_by_ref() → calls schedule_fn(…, Wake)
//   ScheduleInfo       { New, Wake } — passed to schedule function

#ifndef EPIX_CXX_MODULE
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <epix/common.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#endif

namespace epix::async_task {

// ── Forward declarations ──────────────────────────────────────────────────

EPIX_EXPORT enum class ScheduleInfo : uint8_t;
EPIX_EXPORT struct Runnable;
EPIX_EXPORT struct Waker;
EPIX_EXPORT template <typename T>
struct [[nodiscard]] Task;
EPIX_EXPORT template <typename T>
struct [[nodiscard]] FallibleTask;

// ── ScheduleInfo ──────────────────────────────────────────────────────────

/**
 * @brief Passed to the schedule function to distinguish first-schedule
 *        from re-wake. Matches `async_task::ScheduleInfo`.
 */
EPIX_EXPORT enum class ScheduleInfo : uint8_t {
    New,  // `Runnable::schedule()` was called
    Wake  // Waker was invoked (task yielded and is ready again)
};

// ── Internal state ────────────────────────────────────────────────────────

namespace internal {

// State flags — matching Rust's bitflag state machine.
// Multiple flags may be set simultaneously (e.g. SCHEDULED | CLOSED).
enum StateFlags : uint8_t {
    SCHEDULED = 1 << 0,  // in executor queue
    RUNNING   = 1 << 1,  // currently being polled
    COMPLETED = 1 << 2,  // finished (success or exception)
    CLOSED    = 1 << 3,  // cancelled
    TASK      = 1 << 4,  // Task handle still alive (cleared by detach/drop)
};

inline bool is_terminal(uint8_t f) noexcept { return (f & (COMPLETED | CLOSED)) != 0; }

/**
 * @brief Type-erased header shared by Runnable, Waker, Task, FallibleTask.
 *
 * One allocation per spawn.  `TaskState<T>` inherits from this to add
 * the typed output slot.  Runnable/Waker hold `shared_ptr<TaskHeader>`;
 * Task/FallibleTask hold `shared_ptr<TaskState<T>>` — same allocation,
 * same control block (upcast is implicit in shared_ptr).
 */
struct TaskHeader {
    std::atomic<uint8_t> flags{SCHEDULED | TASK};

    // User-provided: called whenever the task needs to be queued.
    std::move_only_function<void(Runnable, ScheduleInfo)> schedule_fn;

    // Type-erased work.  Re-entrant: may be called multiple times for
    // coroutine-based tasks.  MUST set the COMPLETED flag (via fetch_or)
    // when the work has genuinely finished.  Plain callables always set
    // COMPLETED on the first invocation.
    std::move_only_function<void()> work;

    // Error slot. Set if work throws.
    std::exception_ptr exception;

    // Coroutine continuations waiting on completion.
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::move_only_function<void()>> waiters;

    TaskHeader() noexcept = default;

    TaskHeader(std::move_only_function<void(Runnable, ScheduleInfo)> sf, std::move_only_function<void()> w) noexcept
        : schedule_fn(std::move(sf)), work(std::move(w)) {}

    void notify_waiters() {
        std::vector<std::move_only_function<void()>> to_resume;
        {
            std::unique_lock lock(mtx);
            to_resume = std::move(waiters);
        }
        cv.notify_all();
        for (auto& f : to_resume) f();
    }
};

/**
 * @brief Typed state: adds the output slot for T.
 */
template <typename T>
struct TaskState : TaskHeader {
    std::optional<T> value;
    using TaskHeader::TaskHeader;
};

template <>
struct TaskState<void> : TaskHeader {
    using TaskHeader::TaskHeader;
};

// ── Awaitable detection traits (type-level, no runtime dependency) ──

template <typename T, typename = void>
struct is_awaitable_impl : std::false_type {};
template <typename T>
struct is_awaitable_impl<T, std::void_t<decltype(std::declval<T&>().await_ready())>> : std::true_type {};
template <typename T>
inline constexpr bool is_awaitable_v = is_awaitable_impl<T>::value;

template <typename T, typename = void>
struct awaitable_result_impl {};
template <typename T>
struct awaitable_result_impl<T, std::void_t<decltype(std::declval<T&>().await_resume())>> {
    using type = decltype(std::declval<T&>().await_resume());
};
template <typename T>
using awaitable_result_t = typename awaitable_result_impl<T>::type;

template <typename T>
struct is_asio_awaitable_impl : std::false_type {};
template <typename T>
struct is_asio_awaitable_impl<asio::awaitable<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_asio_awaitable_v = is_asio_awaitable_impl<T>::value;
template <typename T>
struct asio_awaitable_value_type {};
template <typename T>
struct asio_awaitable_value_type<asio::awaitable<T>> {
    using type = T;
};

}  // namespace internal

// ── Runnable ──────────────────────────────────────────────────────────────

/**
 * @brief Type-erased, schedulable handle for executors.
 *
 * Matches `async_task::Runnable`:
 *   - `run()` polls once, returns true if re-woken during execution
 *   - `schedule()` queues onto the executor
 *   - `waker()` creates a Waker for re-scheduling
 */
EPIX_EXPORT struct Runnable {
   private:
    std::shared_ptr<internal::TaskHeader> m_header;

    friend struct Waker;
    template <typename T>
    friend struct Task;
    template <typename T>
    friend struct FallibleTask;

   public:
    Runnable() noexcept = default;
    explicit Runnable(std::shared_ptr<internal::TaskHeader> h) noexcept : m_header(std::move(h)) {}

    Runnable(Runnable&&)                 = default;
    Runnable& operator=(Runnable&&)      = default;
    Runnable(const Runnable&)            = default;
    Runnable& operator=(const Runnable&) = default;

    /**
     * @brief Execute the stored work once.
     *
     * Atomically clears SCHEDULED and sets RUNNING, then executes.
     * After execution, clears RUNNING.  If the work completed, sets
     * COMPLETED.  If SCHEDULED was re-set during execution (by a
     * concurrent waker call), returns true — the executor should
     * re-schedule the task.
     *
     * Matches `async_task::Runnable::run()` → bool.
     */
    [[nodiscard]] bool run() {
        if (!m_header) return false;

        // ── CAS: clear SCHEDULED, set RUNNING ──
        uint8_t cur = m_header->flags.load(std::memory_order_acquire);
        while (true) {
            if ((cur & internal::SCHEDULED) == 0) return false;                       // nothing to run
            if ((cur & (internal::COMPLETED | internal::CLOSED)) != 0) return false;  // terminal
            uint8_t next = (cur & ~internal::SCHEDULED) | internal::RUNNING;
            if (m_header->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) break;
        }

        // ── Execute ──
        try {
            m_header->work();
        } catch (...) {
            m_header->exception = std::current_exception();
            // Work threw — mark COMPLETED so the exception is observable.
            m_header->flags.fetch_or(internal::COMPLETED, std::memory_order_release);
        }

        // ── Post-execution: just clear RUNNING ──
        //
        // COMPLETED is set by the work function itself when it finishes
        // (or by the catch block above on exception).  Coroutine work
        // may return without setting COMPLETED — the task stays alive
        // for the next run() invocation.
        bool was_re_woken = false;
        cur               = m_header->flags.load(std::memory_order_acquire);
        while (true) {
            uint8_t next = cur & ~internal::RUNNING;
            if ((cur & internal::SCHEDULED) != 0) {
                was_re_woken = true;
            }
            if (m_header->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) break;
        }

        if (internal::is_terminal(m_header->flags.load(std::memory_order_acquire))) {
            m_header->notify_waiters();
        }

        return was_re_woken;
    }

    /**
     * @brief Schedule the task onto the executor.
     *
     * Atomically sets SCHEDULED, then calls the stored schedule function
     * with `ScheduleInfo::New`.  No-op if the task is terminal.
     *
     * Matches `async_task::Runnable::schedule()`.
     */
    void schedule() {
        if (!m_header || internal::is_terminal(m_header->flags.load(std::memory_order_acquire))) return;
        m_header->flags.fetch_or(internal::SCHEDULED, std::memory_order_acq_rel);
        m_header->schedule_fn(*this, ScheduleInfo::New);
    }

    /**
     * @brief Create a copyable Waker that re-schedules this task.
     *
     * Matches `async_task::Runnable::waker()`.
     */
    [[nodiscard]] Waker waker() const;

    /// True if the task is completed or closed.
    [[nodiscard]] bool is_done() const noexcept {
        return !m_header || internal::is_terminal(m_header->flags.load(std::memory_order_acquire));
    }

    explicit operator bool() const noexcept { return m_header != nullptr; }
    std::shared_ptr<internal::TaskHeader> header() const noexcept { return m_header; }
};

// ── Waker ─────────────────────────────────────────────────────────────────

/**
 * @brief A copyable handle that re-schedules the task when woken.
 *
 * Created by `Runnable::waker()`.  When `wake()` or `wake_by_ref()`
 * is called, sets SCHEDULED and calls the schedule function with
 * `ScheduleInfo::Wake`.
 *
 * Matches `RawWaker` / `Wake` trait in async-task.
 */
EPIX_EXPORT struct Waker {
   private:
    std::shared_ptr<internal::TaskHeader> m_header;

    friend struct Runnable;
    explicit Waker(std::shared_ptr<internal::TaskHeader> h) noexcept : m_header(std::move(h)) {}

   public:
    Waker() noexcept               = default;
    Waker(const Waker&)            = default;
    Waker& operator=(const Waker&) = default;
    Waker(Waker&&)                 = default;
    Waker& operator=(Waker&&)      = default;

    /**
     * @brief Wake the task, consuming this Waker.
     *
     * Sets SCHEDULED, then calls schedule_fn(Runnable, Wake).
     * No-op if the task is terminal.
     *
     * Matches `Wake::wake(self)` in Rust.
     */
    void wake() && {
        if (!m_header || internal::is_terminal(m_header->flags.load(std::memory_order_acquire))) return;
        m_header->flags.fetch_or(internal::SCHEDULED, std::memory_order_acq_rel);
        auto h = std::move(m_header);
        h->schedule_fn(Runnable(std::move(h)), ScheduleInfo::Wake);
    }

    /**
     * @brief Wake the task by reference.
     *
     * Copies the shared_ptr to create a new Runnable.
     *
     * Matches `Wake::wake_by_ref(&self)` in Rust.
     */
    void wake_by_ref() const {
        if (!m_header || internal::is_terminal(m_header->flags.load(std::memory_order_acquire))) return;
        m_header->flags.fetch_or(internal::SCHEDULED, std::memory_order_acq_rel);
        m_header->schedule_fn(Runnable(m_header), ScheduleInfo::Wake);
    }

    explicit operator bool() const noexcept { return m_header != nullptr; }
    bool operator==(const Waker& o) const noexcept { return m_header == o.m_header; }
};

inline Waker Runnable::waker() const { return Waker(m_header); }

// ── Task<T> ───────────────────────────────────────────────────────────────

/**
 * @brief A spawned, awaitable task handle.
 *
 * `[[nodiscard]]`: the destructor **cancels** the task.
 * Use `.detach()` to let it run in the background.
 *
 * Matches `async_task::Task<T>`:
 *   - `cancel()` async → Option<T>
 *   - `fallible()` → FallibleTask<T>
 *   - `detach()` release without cancel
 *   - `is_finished()` non-blocking poll
 *   - `co_await` via asio (replaces `impl Future<Output = T>`)
 *   - `Drop` → cancel + detach
 *   - `#[must_use]`
 */
EPIX_EXPORT template <typename T>
struct [[nodiscard]] Task {
   private:
    std::shared_ptr<internal::TaskState<T>> m_state;

    friend struct FallibleTask<T>;

    // ── Rust: set_canceled ────────────────────────────────────────────
    //
    // CAS loop: add CLOSED.  If not already scheduled/running, also set
    // SCHEDULED and schedule one last time (so the work destructor runs
    // on the executor).  Notifies any awaiter.
    void set_canceled() noexcept {
        if (!m_state) return;
        while (true) {
            auto cur = m_state->flags.load(std::memory_order_acquire);
            if ((cur & (internal::COMPLETED | internal::CLOSED)) != 0) return;  // already terminal
            uint8_t next = cur | internal::CLOSED;
            if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0) {
                // Not yet scheduled/running → schedule one last time for cleanup.
                next |= internal::SCHEDULED;
            }
            if (m_state->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) {
                if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0 && (cur & internal::COMPLETED) == 0) {
                    m_state->schedule_fn(Runnable(m_state), ScheduleInfo::New);
                }
                m_state->notify_waiters();
                return;
            }
        }
    }

    // ── Rust: set_detached ────────────────────────────────────────────
    //
    // Clears the TASK flag.  If terminal, may schedule cleanup.
    void set_detached() noexcept {
        if (!m_state) return;
        m_state->flags.fetch_and(~internal::TASK, std::memory_order_acq_rel);
    }

   public:
    Task() noexcept = default;
    explicit Task(std::shared_ptr<internal::TaskState<T>> s) noexcept : m_state(std::move(s)) {}

    Task(Task&&)                 = default;
    Task& operator=(Task&&)      = default;
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    // ── Rust: Drop ────────────────────────────────────────────────────
    //
    //   fn drop(&mut self) {
    //       self.set_canceled();
    //       self.set_detached();
    //   }
    ~Task() {
        if (!m_state) return;
        set_canceled();
        set_detached();
    }

    // ── Rust: pub async fn cancel(self) -> Option<T> ──────────────────
    //
    // Returns an awaitable that suspends the caller until the task is
    // terminal, then returns Some(value) if completed successfully or
    // nullopt if cancelled/failed.
    [[nodiscard]] auto cancel() && {
        set_canceled();
        struct cancel_awaiter {
            std::shared_ptr<internal::TaskState<T>> state;
            bool await_ready() const noexcept {
                return !state || internal::is_terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (!state) return (void)h.resume();
                std::unique_lock lock(state->mtx);
                if (!internal::is_terminal(state->flags.load(std::memory_order_acquire)))
                    state->waiters.push_back([h]() mutable { h.resume(); });
                else {
                    lock.unlock();
                    h.resume();
                }
            }
            std::optional<T> await_resume() {
                if (!state) return std::nullopt;
                auto f = state->flags.load(std::memory_order_acquire);
                if ((f & internal::COMPLETED) != 0 && !state->exception) return std::move(state->value);
                return std::nullopt;
            }
        };
        return cancel_awaiter{std::move(m_state)};
    }

    // ── Rust: pub fn fallible(self) -> FallibleTask<T> ────────────────
    /**
     * @brief Convert into a FallibleTask (returns optional, never throws).
     */
    // Defined out-of-line after FallibleTask<T> is complete.
    [[nodiscard]] FallibleTask<T> fallible() &&;

    // ── Rust: pub fn detach(self) ─────────────────────────────────────
    //
    //   pub fn detach(self) {
    //       let mut this = self;
    //       let _out = this.set_detached();
    //       mem::forget(this);  // skip Drop
    //   }
    /**
     * @brief Detach: let the task run independently.  Does NOT cancel.
     */
    void detach() noexcept {
        if (!m_state) return;
        set_detached();
        m_state.reset();  // release ref without running ~Task
    }

    // ── Rust: pub fn is_finished(&self) -> bool ───────────────────────
    //
    //   pub fn is_finished(&self) -> bool {
    //       let state = (*header).state.load(Ordering::Acquire);
    //       state & (CLOSED | COMPLETED) != 0
    //   }
    /**
     * @brief True if the task is completed or closed.
     * Note: racy in multi-threaded contexts.
     */
    [[nodiscard]] bool is_finished() const noexcept {
        if (!m_state) return true;
        auto f = m_state->flags.load(std::memory_order_acquire);
        return (f & (internal::COMPLETED | internal::CLOSED)) != 0;
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    // ── C++20 awaitable protocol (Rust Future<Output = T> equivalent) ──

    Task& operator co_await() & noexcept { return *this; }
    Task&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }

    template <typename H>
    void await_suspend(H h) {
        std::unique_lock lock(m_state->mtx);
        if (!internal::is_terminal(m_state->flags.load(std::memory_order_acquire))) {
            m_state->waiters.push_back([h]() mutable { h.resume(); });
            return;
        }
        lock.unlock();
        h.resume();
    }

    T await_resume() {
        if (m_state->exception) std::rethrow_exception(m_state->exception);
        return std::move(*m_state->value);
    }

    template <ASIO_COMPLETION_TOKEN_FOR(void(std::exception_ptr, T)) CompletionToken>
    auto operator()(CompletionToken&& token) && {
        return asio::async_initiate<CompletionToken, void(std::exception_ptr, T)>(
            [state = std::move(m_state)](auto handler) mutable {
                auto ex     = asio::get_associated_executor(handler);
                auto invoke = [state, h = std::move(handler)]() mutable {
                    auto f = state->flags.load(std::memory_order_acquire);
                    if ((f & (internal::COMPLETED | internal::CLOSED)) == 0) {
                        h(std::make_exception_ptr(std::runtime_error("task polled before completion")), T{});
                        return;
                    }
                    if ((f & internal::COMPLETED) != 0 && !state->exception) {
                        if constexpr (!std::is_void_v<T>)
                            h(nullptr, std::move(*state->value));
                        else
                            h(nullptr);
                    } else {
                        h(state->exception ? state->exception
                                           : std::make_exception_ptr(std::runtime_error("task cancelled")),
                          T{});
                    }
                };
                {
                    std::unique_lock lock(state->mtx);
                    if (!internal::is_terminal(state->flags.load(std::memory_order_acquire))) {
                        state->waiters.push_back(
                            [invoke = std::move(invoke), ex]() mutable { asio::post(ex, std::move(invoke)); });
                        return;
                    }
                }
                asio::post(ex, std::move(invoke));
            },
            token);
    }

    static std::pair<Task, std::shared_ptr<internal::TaskState<T>>> make() {
        auto s = std::make_shared<internal::TaskState<T>>();
        return {Task{s}, s};
    }
};

// ── Task<void> ────────────────────────────────────────────────────────────

template <>
struct [[nodiscard]] Task<void> {
   private:
    std::shared_ptr<internal::TaskState<void>> m_state;

    friend struct FallibleTask<void>;

    void set_canceled() noexcept {
        if (!m_state) return;
        while (true) {
            auto cur = m_state->flags.load(std::memory_order_acquire);
            if ((cur & (internal::COMPLETED | internal::CLOSED)) != 0) return;
            uint8_t next = cur | internal::CLOSED;
            if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0) {
                next |= internal::SCHEDULED;
            }
            if (m_state->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) {
                if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0 && (cur & internal::COMPLETED) == 0) {
                    m_state->schedule_fn(Runnable(m_state), ScheduleInfo::New);
                }
                m_state->notify_waiters();
                return;
            }
        }
    }

    void set_detached() noexcept {
        if (!m_state) return;
        m_state->flags.fetch_and(~internal::TASK, std::memory_order_acq_rel);
    }

   public:
    Task() noexcept = default;
    explicit Task(std::shared_ptr<internal::TaskState<void>> s) noexcept : m_state(std::move(s)) {}

    Task(Task&&)                 = default;
    Task& operator=(Task&&)      = default;
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (!m_state) return;
        set_canceled();
        set_detached();
    }

    [[nodiscard]] auto cancel() && {
        set_canceled();
        struct cancel_awaiter {
            std::shared_ptr<internal::TaskState<void>> state;
            bool await_ready() const noexcept {
                return !state || internal::is_terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (!state) return (void)h.resume();
                std::unique_lock lock(state->mtx);
                if (!internal::is_terminal(state->flags.load(std::memory_order_acquire)))
                    state->waiters.push_back([h]() mutable { h.resume(); });
                else {
                    lock.unlock();
                    h.resume();
                }
            }
            bool await_resume() {
                if (!state) return false;
                auto f = state->flags.load(std::memory_order_acquire);
                return (f & internal::COMPLETED) != 0 && !state->exception;
            }
        };
        return cancel_awaiter{std::move(m_state)};
    }

    // Defined out-of-line after FallibleTask<void> is complete.
    [[nodiscard]] FallibleTask<void> fallible() &&;

    void detach() noexcept {
        if (!m_state) return;
        set_detached();
        m_state.reset();
    }

    [[nodiscard]] bool is_finished() const noexcept {
        if (!m_state) return true;
        auto f = m_state->flags.load(std::memory_order_acquire);
        return (f & (internal::COMPLETED | internal::CLOSED)) != 0;
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    // ── C++20 awaitable protocol ──────────────────────────────────

    Task& operator co_await() & noexcept { return *this; }
    Task&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }

    void await_suspend(std::coroutine_handle<> h) {
        std::unique_lock lock(m_state->mtx);
        if (!internal::is_terminal(m_state->flags.load(std::memory_order_acquire))) {
            m_state->waiters.push_back([h]() mutable { h.resume(); });
            return;
        }
        lock.unlock();
        h.resume();
    }

    void await_resume() {
        if (m_state->exception) std::rethrow_exception(m_state->exception);
    }

    template <ASIO_COMPLETION_TOKEN_FOR(void(std::exception_ptr)) CompletionToken>
    auto operator()(CompletionToken&& token) && {
        return asio::async_initiate<CompletionToken, void(std::exception_ptr)>(
            [state = std::move(m_state)](auto handler) mutable {
                auto ex     = asio::get_associated_executor(handler);
                auto invoke = [state, h = std::move(handler)]() mutable {
                    auto f = state->flags.load(std::memory_order_acquire);
                    if ((f & (internal::COMPLETED | internal::CLOSED)) == 0) {
                        h(std::make_exception_ptr(std::runtime_error("task polled before completion")));
                        return;
                    }
                    if ((f & internal::COMPLETED) != 0 && !state->exception) {
                        h(nullptr);
                    } else {
                        h(state->exception ? state->exception
                                           : std::make_exception_ptr(std::runtime_error("task cancelled")));
                    }
                };
                {
                    std::unique_lock lock(state->mtx);
                    if (!internal::is_terminal(state->flags.load(std::memory_order_acquire))) {
                        state->waiters.push_back(
                            [invoke = std::move(invoke), ex]() mutable { asio::post(ex, std::move(invoke)); });
                        return;
                    }
                }
                asio::post(ex, std::move(invoke));
            },
            token);
    }

    static std::pair<Task, std::shared_ptr<internal::TaskState<void>>> make() {
        auto s = std::make_shared<internal::TaskState<void>>();
        return {Task{s}, s};
    }
};

// ── FallibleTask<T> ───────────────────────────────────────────────────────

/**
 * @brief Like Task but `co_await` returns `std::optional<T>`.
 *
 * `Some(value)` on success, `std::nullopt` if cancelled or failed.
 */
EPIX_EXPORT template <typename T>
struct [[nodiscard]] FallibleTask {
   private:
    std::shared_ptr<internal::TaskState<T>> m_state;

    void set_canceled() noexcept {
        if (!m_state) return;
        while (true) {
            auto cur = m_state->flags.load(std::memory_order_acquire);
            if ((cur & (internal::COMPLETED | internal::CLOSED)) != 0) return;
            uint8_t next = cur | internal::CLOSED;
            if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0) {
                next |= internal::SCHEDULED;
            }
            if (m_state->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) {
                if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0 && (cur & internal::COMPLETED) == 0) {
                    m_state->schedule_fn(Runnable(m_state), ScheduleInfo::New);
                }
                m_state->notify_waiters();
                return;
            }
        }
    }

    void set_detached() noexcept {
        if (!m_state) return;
        m_state->flags.fetch_and(~internal::TASK, std::memory_order_acq_rel);
    }

   public:
    FallibleTask() noexcept = default;
    explicit FallibleTask(std::shared_ptr<internal::TaskState<T>> s) noexcept : m_state(std::move(s)) {}

    FallibleTask(FallibleTask&&)                 = default;
    FallibleTask& operator=(FallibleTask&&)      = default;
    FallibleTask(const FallibleTask&)            = delete;
    FallibleTask& operator=(const FallibleTask&) = delete;

    ~FallibleTask() {
        if (!m_state) return;
        set_canceled();
        set_detached();
    }

    [[nodiscard]] auto cancel() && {
        set_canceled();
        struct cancel_awaiter {
            std::shared_ptr<internal::TaskState<T>> state;
            bool await_ready() const noexcept {
                return !state || internal::is_terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (!state) return (void)h.resume();
                std::unique_lock lock(state->mtx);
                if (!internal::is_terminal(state->flags.load(std::memory_order_acquire)))
                    state->waiters.push_back([h]() mutable { h.resume(); });
                else {
                    lock.unlock();
                    h.resume();
                }
            }
            std::optional<T> await_resume() {
                if (!state) return std::nullopt;
                auto f = state->flags.load(std::memory_order_acquire);
                if ((f & internal::COMPLETED) != 0 && !state->exception) return std::move(state->value);
                return std::nullopt;
            }
        };
        return cancel_awaiter{std::move(m_state)};
    }

    [[nodiscard]] FallibleTask fallible() && { return std::move(*this); }

    void detach() noexcept {
        if (!m_state) return;
        set_detached();
        m_state.reset();
    }

    [[nodiscard]] bool is_finished() const noexcept {
        if (!m_state) return true;
        auto f = m_state->flags.load(std::memory_order_acquire);
        return (f & (internal::COMPLETED | internal::CLOSED)) != 0;
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    // ── C++20 awaitable protocol → std::optional<T> ──────────────

    FallibleTask& operator co_await() & noexcept { return *this; }
    FallibleTask&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }

    void await_suspend(std::coroutine_handle<> h) {
        std::unique_lock lock(m_state->mtx);
        if (!internal::is_terminal(m_state->flags.load(std::memory_order_acquire))) {
            m_state->waiters.push_back([h]() mutable { h.resume(); });
            return;
        }
        lock.unlock();
        h.resume();
    }

    std::optional<T> await_resume() {
        auto f = m_state->flags.load(std::memory_order_acquire);
        if ((f & internal::COMPLETED) != 0 && !m_state->exception) return std::move(m_state->value);
        return std::nullopt;
    }

    template <ASIO_COMPLETION_TOKEN_FOR(void(std::exception_ptr, std::optional<T>)) CompletionToken>
    auto operator()(CompletionToken&& token) && {
        return asio::async_initiate<CompletionToken, void(std::exception_ptr, std::optional<T>)>(
            [state = std::move(m_state)](auto handler) mutable {
                auto ex     = asio::get_associated_executor(handler);
                auto invoke = [state, h = std::move(handler)]() mutable {
                    auto f = state->flags.load(std::memory_order_acquire);
                    if ((f & internal::COMPLETED) != 0 && !state->exception)
                        h(nullptr, std::move(state->value));
                    else
                        h(nullptr, std::nullopt);
                };
                {
                    std::unique_lock lock(state->mtx);
                    if (!internal::is_terminal(state->flags.load(std::memory_order_acquire))) {
                        state->waiters.push_back(
                            [invoke = std::move(invoke), ex]() mutable { asio::post(ex, std::move(invoke)); });
                        return;
                    }
                }
                asio::post(ex, std::move(invoke));
            },
            token);
    }
};

// ── FallibleTask<void> ────────────────────────────────────────────────────

template <>
struct [[nodiscard]] FallibleTask<void> {
   private:
    std::shared_ptr<internal::TaskState<void>> m_state;

    void set_canceled() noexcept {
        if (!m_state) return;
        while (true) {
            auto cur = m_state->flags.load(std::memory_order_acquire);
            if ((cur & (internal::COMPLETED | internal::CLOSED)) != 0) return;
            uint8_t next = cur | internal::CLOSED;
            if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0) {
                next |= internal::SCHEDULED;
            }
            if (m_state->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) {
                if ((cur & (internal::SCHEDULED | internal::RUNNING)) == 0 && (cur & internal::COMPLETED) == 0) {
                    m_state->schedule_fn(Runnable(m_state), ScheduleInfo::New);
                }
                m_state->notify_waiters();
                return;
            }
        }
    }

    void set_detached() noexcept {
        if (!m_state) return;
        m_state->flags.fetch_and(~internal::TASK, std::memory_order_acq_rel);
    }

   public:
    FallibleTask() noexcept = default;
    explicit FallibleTask(std::shared_ptr<internal::TaskState<void>> s) noexcept : m_state(std::move(s)) {}

    FallibleTask(FallibleTask&&)                 = default;
    FallibleTask& operator=(FallibleTask&&)      = default;
    FallibleTask(const FallibleTask&)            = delete;
    FallibleTask& operator=(const FallibleTask&) = delete;

    ~FallibleTask() {
        if (!m_state) return;
        set_canceled();
        set_detached();
    }

    [[nodiscard]] auto cancel() && {
        set_canceled();
        struct cancel_awaiter {
            std::shared_ptr<internal::TaskState<void>> state;
            bool await_ready() const noexcept {
                return !state || internal::is_terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (!state) return (void)h.resume();
                std::unique_lock lock(state->mtx);
                if (!internal::is_terminal(state->flags.load(std::memory_order_acquire)))
                    state->waiters.push_back([h]() mutable { h.resume(); });
                else {
                    lock.unlock();
                    h.resume();
                }
            }
            bool await_resume() {
                if (!state) return false;
                auto f = state->flags.load(std::memory_order_acquire);
                return (f & internal::COMPLETED) != 0 && !state->exception;
            }
        };
        return cancel_awaiter{std::move(m_state)};
    }

    [[nodiscard]] FallibleTask fallible() && { return std::move(*this); }

    void detach() noexcept {
        if (!m_state) return;
        set_detached();
        m_state.reset();
    }

    [[nodiscard]] bool is_finished() const noexcept {
        if (!m_state) return true;
        auto f = m_state->flags.load(std::memory_order_acquire);
        return (f & (internal::COMPLETED | internal::CLOSED)) != 0;
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    // ── C++20 awaitable protocol → bool ───────────────────────────

    FallibleTask& operator co_await() & noexcept { return *this; }
    FallibleTask&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }

    void await_suspend(std::coroutine_handle<> h) {
        std::unique_lock lock(m_state->mtx);
        if (!internal::is_terminal(m_state->flags.load(std::memory_order_acquire))) {
            m_state->waiters.push_back([h]() mutable { h.resume(); });
            return;
        }
        lock.unlock();
        h.resume();
    }

    bool await_resume() {
        auto f = m_state->flags.load(std::memory_order_acquire);
        return (f & internal::COMPLETED) != 0 && !m_state->exception;
    }

    template <ASIO_COMPLETION_TOKEN_FOR(void(std::exception_ptr, bool)) CompletionToken>
    auto operator()(CompletionToken&& token) && {
        return asio::async_initiate<CompletionToken, void(std::exception_ptr, bool)>(
            [state = std::move(m_state)](auto handler) mutable {
                auto ex     = asio::get_associated_executor(handler);
                auto invoke = [state, h = std::move(handler)]() mutable {
                    auto f  = state->flags.load(std::memory_order_acquire);
                    bool ok = (f & internal::COMPLETED) != 0 && !state->exception;
                    h(nullptr, ok);
                };
                {
                    std::unique_lock lock(state->mtx);
                    if (!internal::is_terminal(state->flags.load(std::memory_order_acquire))) {
                        state->waiters.push_back(
                            [invoke = std::move(invoke), ex]() mutable { asio::post(ex, std::move(invoke)); });
                        return;
                    }
                }
                asio::post(ex, std::move(invoke));
            },
            token);
    }
};

// ── Out-of-line definitions (require complete types) ────────────────────

template <typename T>
FallibleTask<T> Task<T>::fallible() && {
    return FallibleTask<T>(std::move(m_state));
}

inline FallibleTask<void> Task<void>::fallible() && { return FallibleTask<void>(std::move(m_state)); }

// ── spawn() ───────────────────────────────────────────────────────────────

/**
 * @brief Spawn work as a task, returning `(Runnable, Task<T>)`.
 *
 * Two paths, selected from F's return type:
 *   - Plain `F() -> T`: one-shot, COMPLETED on first run().
 *   - Awaitable `F() -> A` (has await_ready/await_resume):
 *       Each run() → work() is one poll.  The awaitable is created once;
 *       on each call await_ready() is checked.  If ready the value is
 *       obtained via await_resume() and COMPLETED is set.  Otherwise
 *       SCHEDULED is set so run() returns true and the executor re-queues.
 *       Between polls the runtime completes async ops, making the
 *       awaitable ready.  Matches Rust Future::poll.
 *
 * Note: await_suspend is NOT called — it must have been invoked internally
 * by the coroutine body during the initial f() call.  Awaitable types whose
 * final_suspend requires an external await_suspend (e.g. asio) need a
 * runtime-specific bridge provided by a higher-level module.
 */
EPIX_EXPORT template <typename F, typename S>
    requires std::invocable<F> && std::invocable<S, Runnable, ScheduleInfo>
[[nodiscard]] auto spawn(F&& work, S&& schedule) {
    using Ret = std::invoke_result_t<F>;

    if constexpr (internal::is_asio_awaitable_v<Ret>) {
        using U    = typename internal::asio_awaitable_value_type<Ret>::type;
        auto state = std::make_shared<internal::TaskState<U>>();
        auto* raw  = state.get();

        state->schedule_fn = std::move_only_function<void(Runnable, ScheduleInfo)>(
            [s = std::forward<S>(schedule)](Runnable r, ScheduleInfo info) mutable {
                std::invoke(s, std::move(r), info);
            });

        state->work = std::move_only_function<void()>(
            [f = std::forward<F>(work), raw, ioc = std::make_shared<asio::io_context>(),
             done = std::make_shared<std::atomic<bool>>(false), spawned = false]() mutable {
                if (!spawned) {
                    spawned = true;
                    asio::co_spawn(
                        *ioc,
                        [raw, done, f = std::move(f)]() -> asio::awaitable<void> {
                            try {
                                if constexpr (!std::is_void_v<U>)
                                    raw->value = co_await std::invoke(std::move(f));
                                else
                                    co_await std::invoke(std::move(f));
                            } catch (...) {
                                raw->exception = std::current_exception();
                            }
                            done->store(true, std::memory_order_release);
                        },
                        asio::detached);
                }
                ioc->poll_one();
                if (done->load(std::memory_order_acquire))
                    raw->flags.fetch_or(internal::COMPLETED, std::memory_order_release);
                else
                    raw->flags.fetch_or(internal::SCHEDULED, std::memory_order_release);
            });

        std::shared_ptr<internal::TaskHeader> base = state;
        return std::pair<Runnable, Task<U>>(Runnable(std::move(base)), Task<U>(std::move(state)));
    } else if constexpr (internal::is_awaitable_v<Ret>) {
        using T    = internal::awaitable_result_t<Ret>;
        auto state = std::make_shared<internal::TaskState<T>>();
        auto* raw  = state.get();

        state->schedule_fn = std::move_only_function<void(Runnable, ScheduleInfo)>(
            [s = std::forward<S>(schedule)](Runnable r, ScheduleInfo info) mutable {
                std::invoke(s, std::move(r), info);
            });

        state->work = std::move_only_function<void()>(
            [f = std::forward<F>(work), raw, state_ptr = std::shared_ptr<Ret>(nullptr), started = false]() mutable {
                if (!started) {
                    started   = true;
                    state_ptr = std::make_shared<Ret>(std::invoke(std::move(f)));
                }
                if (state_ptr->await_ready()) {
                    if constexpr (!std::is_void_v<T>)
                        raw->value = state_ptr->await_resume();
                    else
                        state_ptr->await_resume();
                    raw->flags.fetch_or(internal::COMPLETED, std::memory_order_release);
                } else {
                    raw->flags.fetch_or(internal::SCHEDULED, std::memory_order_release);
                }
            });

        std::shared_ptr<internal::TaskHeader> base = state;
        return std::pair<Runnable, Task<T>>(Runnable(std::move(base)), Task<T>(std::move(state)));
    } else {
        using T    = Ret;
        auto state = std::make_shared<internal::TaskState<T>>();

        state->schedule_fn = std::move_only_function<void(Runnable, ScheduleInfo)>(
            [s = std::forward<S>(schedule)](Runnable r, ScheduleInfo info) mutable {
                std::invoke(s, std::move(r), info);
            });

        if constexpr (std::is_void_v<T>) {
            state->work = std::move_only_function<void()>([f = std::forward<F>(work), raw = state.get()]() mutable {
                std::invoke(std::move(f));
                raw->flags.fetch_or(internal::COMPLETED, std::memory_order_release);
            });
        } else {
            auto* raw   = state.get();
            state->work = std::move_only_function<void()>([f = std::forward<F>(work), raw]() mutable {
                raw->value = std::invoke(std::move(f));
                raw->flags.fetch_or(internal::COMPLETED, std::memory_order_release);
            });
        }

        std::shared_ptr<internal::TaskHeader> base = state;
        return std::pair<Runnable, Task<T>>(Runnable(std::move(base)), Task<T>(std::move(state)));
    }
}

}  // namespace epix::async_task

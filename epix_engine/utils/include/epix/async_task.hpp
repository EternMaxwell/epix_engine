#pragma once

#ifndef EPIX_CXX_MODULE
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <epix/common.hpp>
#include <exception>
#include <exec/start_detached.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stdexec/execution.hpp>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <expected>
#endif

namespace epix::async_task {

EPIX_EXPORT enum class ScheduleInfo : std::uint8_t {
    New,
    Wake,
};

EPIX_EXPORT struct Runnable;
EPIX_EXPORT struct Waker;

EPIX_EXPORT template <typename T>
struct [[nodiscard]] Task;

EPIX_EXPORT template <typename T>
struct [[nodiscard]] FallibleTask;

namespace internal {

enum StateFlags : std::uint8_t {
    Scheduled = 1 << 0,
    Running   = 1 << 1,
    Completed = 1 << 2,
    Closed    = 1 << 3,
};

inline bool terminal(std::uint8_t flags) noexcept { return (flags & (Completed | Closed)) != 0; }

struct TaskHeader {
    std::atomic<std::uint8_t> flags{0};
    std::move_only_function<void(std::shared_ptr<TaskHeader>, ScheduleInfo)> schedule_fn;
    std::exception_ptr exception;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::coroutine_handle<>> waiters;

    virtual ~TaskHeader() = default;
    virtual void poll()   = 0;

    bool mark_completed() noexcept {
        auto cur = flags.load(std::memory_order_acquire);
        while (!terminal(cur)) {
            auto next = static_cast<std::uint8_t>(cur | Completed);
            if (flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) {
                notify_waiters();
                return true;
            }
        }
        notify_waiters();
        return false;
    }

    void mark_closed() noexcept {
        auto cur = flags.load(std::memory_order_acquire);
        while (!terminal(cur)) {
            auto next = static_cast<std::uint8_t>(cur | Closed);
            if (flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) break;
        }
        notify_waiters();
    }

    void fail(std::exception_ptr ex) noexcept {
        exception = ex;
        this->mark_completed();
    }

    void notify_waiters() {
        std::vector<std::coroutine_handle<>> ready;
        {
            std::lock_guard lock(mtx);
            ready.swap(waiters);
        }
        cv.notify_all();
        for (auto handle : ready) {
            if (handle) handle.resume();
        }
    }

    void wait_or_resume(std::coroutine_handle<> handle) {
        std::unique_lock lock(mtx);
        if (!terminal(flags.load(std::memory_order_acquire))) {
            waiters.push_back(handle);
            return;
        }
        lock.unlock();
        handle.resume();
    }
};

template <typename T>
struct TaskState final : TaskHeader {
    std::optional<T> value;
    std::move_only_function<T()> work;

    void poll() override {
        value.emplace(work());
        this->mark_completed();
    }

    template <typename U>
    void complete(U&& v) {
        value.emplace(std::forward<U>(v));
        this->mark_completed();
    }
};

template <>
struct TaskState<void> final : TaskHeader {
    std::move_only_function<void()> work;

    void poll() override {
        work();
        this->mark_completed();
    }

    void complete() { this->mark_completed(); }
};

template <typename T>
struct always_false : std::false_type {};

template <typename T>
struct task_value_from_tuple;

template <>
struct task_value_from_tuple<std::tuple<>> {
    using type = void;
};

template <typename T>
struct task_value_from_tuple<std::tuple<T>> {
    using type = T;
};

template <typename T, typename U, typename... Rest>
struct task_value_from_tuple<std::tuple<T, U, Rest...>> {
    static_assert(always_false<T>::value, "epix::async_task::Task supports senders with zero or one value");
};

template <typename... Ts>
using decayed_tuple = std::tuple<std::decay_t<Ts>...>;

template <typename Sender>
struct sender_task_value {
    using type = typename task_value_from_tuple<
        STDEXEC::value_types_of_t<Sender, STDEXEC::env<>, decayed_tuple, std::type_identity_t>>::type;
};

template <typename T, typename Env>
struct sender_task_value<STDEXEC::task<T, Env>> {
    using type = T;
};

template <typename Sender>
using sender_task_value_t = typename sender_task_value<std::remove_cvref_t<Sender>>::type;

template <typename T>
struct is_stdexec_task : std::false_type {};

template <typename T, typename Env>
struct is_stdexec_task<STDEXEC::task<T, Env>> : std::true_type {};

template <typename T>
inline constexpr bool is_stdexec_task_v = is_stdexec_task<std::remove_cvref_t<T>>::value;

inline std::exception_ptr to_exception_ptr(std::exception_ptr ex) noexcept { return ex; }

template <typename E>
std::exception_ptr to_exception_ptr(E&& error) noexcept {
    try {
        if constexpr (std::derived_from<std::remove_cvref_t<E>, std::exception>)
            return std::make_exception_ptr(std::forward<E>(error));
        else
            return std::make_exception_ptr(std::runtime_error("sender completed with non-exception error"));
    } catch (...) {
        return std::current_exception();
    }
}

}  // namespace internal

EPIX_EXPORT struct Runnable {
   private:
    std::shared_ptr<internal::TaskHeader> m_state;

    friend struct Waker;

   public:
    Runnable() noexcept = default;
    explicit Runnable(std::shared_ptr<internal::TaskHeader> state) noexcept : m_state(std::move(state)) {}

    Runnable(Runnable&&) noexcept            = default;
    Runnable& operator=(Runnable&&) noexcept = default;
    Runnable(const Runnable&)                = default;
    Runnable& operator=(const Runnable&)     = default;

    [[nodiscard]] bool run() {
        if (!m_state) return false;

        auto cur = m_state->flags.load(std::memory_order_acquire);
        while (true) {
            if (internal::terminal(cur) || (cur & internal::Scheduled) == 0) return false;
            auto next = static_cast<std::uint8_t>((cur & ~internal::Scheduled) | internal::Running);
            if (m_state->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) break;
        }

        try {
            m_state->poll();
        } catch (...) {
            m_state->fail(std::current_exception());
        }

        bool rescheduled = false;
        cur              = m_state->flags.load(std::memory_order_acquire);
        while (true) {
            rescheduled = (cur & internal::Scheduled) != 0 && !internal::terminal(cur);
            auto next   = static_cast<std::uint8_t>(cur & ~internal::Running);
            if (m_state->flags.compare_exchange_weak(cur, next, std::memory_order_acq_rel)) break;
        }
        return rescheduled;
    }

    void schedule() {
        if (!m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire))) return;
        m_state->flags.fetch_or(internal::Scheduled, std::memory_order_acq_rel);
        if (m_state->schedule_fn) m_state->schedule_fn(m_state, ScheduleInfo::New);
    }

    [[nodiscard]] Waker waker() const;

    [[nodiscard]] bool is_done() const noexcept {
        return !m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire));
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }
};

EPIX_EXPORT struct Waker {
   private:
    std::shared_ptr<internal::TaskHeader> m_state;

    friend struct Runnable;
    explicit Waker(std::shared_ptr<internal::TaskHeader> state) noexcept : m_state(std::move(state)) {}

   public:
    Waker() noexcept               = default;
    Waker(const Waker&)            = default;
    Waker& operator=(const Waker&) = default;
    Waker(Waker&&) noexcept        = default;
    Waker& operator=(Waker&&)      = default;

    void wake() && {
        auto state = std::move(m_state);
        if (!state || internal::terminal(state->flags.load(std::memory_order_acquire))) return;
        state->flags.fetch_or(internal::Scheduled, std::memory_order_acq_rel);
        if (state->schedule_fn) {
            state->schedule_fn(state, ScheduleInfo::Wake);
        }
    }

    void wake_by_ref() const {
        if (!m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire))) return;
        m_state->flags.fetch_or(internal::Scheduled, std::memory_order_acq_rel);
        if (m_state->schedule_fn) {
            m_state->schedule_fn(m_state, ScheduleInfo::Wake);
        }
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }
    bool operator==(const Waker& other) const noexcept { return m_state == other.m_state; }
};

inline Waker Runnable::waker() const { return Waker(m_state); }

template <typename T>
struct [[nodiscard]] Task {
   private:
    std::shared_ptr<internal::TaskState<T>> m_state;

    friend struct FallibleTask<T>;

    void close() noexcept {
        if (m_state) m_state->mark_closed();
    }

   public:
    Task() noexcept = default;
    explicit Task(std::shared_ptr<internal::TaskState<T>> state) noexcept : m_state(std::move(state)) {}

    Task(Task&&) noexcept            = default;
    Task& operator=(Task&&) noexcept = default;
    Task(const Task&)                = delete;
    Task& operator=(const Task&)     = delete;

    ~Task() { close(); }

    [[nodiscard]] auto cancel() && {
        auto state = std::exchange(m_state, nullptr);
        if (state) state->mark_closed();
        struct awaiter {
            std::shared_ptr<internal::TaskState<T>> state;
            bool await_ready() const noexcept {
                return !state || internal::terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (state)
                    state->wait_or_resume(h);
                else
                    h.resume();
            }
            std::optional<T> await_resume() {
                if (!state || state->exception) return std::nullopt;
                auto flags = state->flags.load(std::memory_order_acquire);
                if ((flags & internal::Completed) == 0 || !state->value) return std::nullopt;
                return std::move(state->value);
            }
        };
        return awaiter{std::move(state)};
    }

    [[nodiscard]] FallibleTask<T> fallible() &&;

    void detach() noexcept { m_state.reset(); }

    [[nodiscard]] bool is_finished() const noexcept {
        return !m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire));
    }

    /** @brief Block the calling thread until the task completes. */
    std::expected<T, std::exception_ptr> block() {
        if (!m_state) return std::unexpected(std::make_exception_ptr(std::runtime_error("block on empty task")));
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [this] { return is_finished(); });
        }
        if (m_state->exception) return std::unexpected(m_state->exception);
        if (!m_state->value) return std::unexpected(std::make_exception_ptr(std::runtime_error("task cancelled")));
        return std::move(*m_state->value);
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    Task& operator co_await() & noexcept { return *this; }
    Task&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }
    void await_suspend(std::coroutine_handle<> h) {
        if (m_state)
            m_state->wait_or_resume(h);
        else
            h.resume();
    }
    T await_resume() {
        if (!m_state) throw std::runtime_error("co_await on empty task");
        if (m_state->exception) std::rethrow_exception(m_state->exception);
        if (!m_state->value) throw std::runtime_error("task cancelled");
        return std::move(*m_state->value);
    }

    static std::pair<Task, std::shared_ptr<internal::TaskState<T>>> make() {
        auto state = std::make_shared<internal::TaskState<T>>();
        return {Task(state), std::move(state)};
    }
};

template <>
struct [[nodiscard]] Task<void> {
   private:
    std::shared_ptr<internal::TaskState<void>> m_state;

    friend struct FallibleTask<void>;

    void close() noexcept {
        if (m_state) m_state->mark_closed();
    }

   public:
    Task() noexcept = default;
    explicit Task(std::shared_ptr<internal::TaskState<void>> state) noexcept : m_state(std::move(state)) {}

    Task(Task&&) noexcept            = default;
    Task& operator=(Task&&) noexcept = default;
    Task(const Task&)                = delete;
    Task& operator=(const Task&)     = delete;

    ~Task() { close(); }

    [[nodiscard]] auto cancel() && {
        auto state = std::exchange(m_state, nullptr);
        if (state) state->mark_closed();
        struct awaiter {
            std::shared_ptr<internal::TaskState<void>> state;
            bool await_ready() const noexcept {
                return !state || internal::terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (state)
                    state->wait_or_resume(h);
                else
                    h.resume();
            }
            bool await_resume() {
                if (!state || state->exception) return false;
                auto flags = state->flags.load(std::memory_order_acquire);
                return (flags & internal::Completed) != 0;
            }
        };
        return awaiter{std::move(state)};
    }

    [[nodiscard]] FallibleTask<void> fallible() &&;

    void detach() noexcept { m_state.reset(); }

    [[nodiscard]] bool is_finished() const noexcept {
        return !m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire));
    }

    /** @brief Block the calling thread until the task completes. */
    std::expected<void, std::exception_ptr> block() {
        if (!m_state) return std::unexpected(std::make_exception_ptr(std::runtime_error("block on empty task")));
        {
            std::unique_lock lock(m_state->mtx);
            m_state->cv.wait(lock, [this] { return is_finished(); });
        }
        if (m_state->exception) return std::unexpected(m_state->exception);
        return {};
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    Task& operator co_await() & noexcept { return *this; }
    Task&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }
    void await_suspend(std::coroutine_handle<> h) {
        if (m_state)
            m_state->wait_or_resume(h);
        else
            h.resume();
    }
    void await_resume() {
        if (!m_state) throw std::runtime_error("co_await on empty task");
        if (m_state->exception) std::rethrow_exception(m_state->exception);
        auto flags = m_state->flags.load(std::memory_order_acquire);
        if ((flags & internal::Completed) == 0) throw std::runtime_error("task cancelled");
    }

    static std::pair<Task, std::shared_ptr<internal::TaskState<void>>> make() {
        auto state = std::make_shared<internal::TaskState<void>>();
        return {Task(state), std::move(state)};
    }
};

template <typename T>
struct [[nodiscard]] FallibleTask {
   private:
    std::shared_ptr<internal::TaskState<T>> m_state;

    void close() noexcept {
        if (m_state) m_state->mark_closed();
    }

   public:
    FallibleTask() noexcept = default;
    explicit FallibleTask(std::shared_ptr<internal::TaskState<T>> state) noexcept : m_state(std::move(state)) {}

    FallibleTask(FallibleTask&&) noexcept            = default;
    FallibleTask& operator=(FallibleTask&&) noexcept = default;
    FallibleTask(const FallibleTask&)                = delete;
    FallibleTask& operator=(const FallibleTask&)     = delete;

    ~FallibleTask() { close(); }

    [[nodiscard]] auto cancel() && {
        auto state = std::exchange(m_state, nullptr);
        if (state) state->mark_closed();
        struct awaiter {
            std::shared_ptr<internal::TaskState<T>> state;
            bool await_ready() const noexcept {
                return !state || internal::terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (state)
                    state->wait_or_resume(h);
                else
                    h.resume();
            }
            std::optional<T> await_resume() {
                if (!state || state->exception) return std::nullopt;
                auto flags = state->flags.load(std::memory_order_acquire);
                if ((flags & internal::Completed) == 0 || !state->value) return std::nullopt;
                return std::move(state->value);
            }
        };
        return awaiter{std::move(state)};
    }

    [[nodiscard]] FallibleTask fallible() && { return std::move(*this); }

    void detach() noexcept { m_state.reset(); }

    [[nodiscard]] bool is_finished() const noexcept {
        return !m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire));
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    FallibleTask& operator co_await() & noexcept { return *this; }
    FallibleTask&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }
    void await_suspend(std::coroutine_handle<> h) {
        if (m_state)
            m_state->wait_or_resume(h);
        else
            h.resume();
    }
    std::optional<T> await_resume() {
        if (!m_state || m_state->exception) return std::nullopt;
        auto flags = m_state->flags.load(std::memory_order_acquire);
        if ((flags & internal::Completed) == 0 || !m_state->value) return std::nullopt;
        return std::move(m_state->value);
    }
};

template <>
struct [[nodiscard]] FallibleTask<void> {
   private:
    std::shared_ptr<internal::TaskState<void>> m_state;

    void close() noexcept {
        if (m_state) m_state->mark_closed();
    }

   public:
    FallibleTask() noexcept = default;
    explicit FallibleTask(std::shared_ptr<internal::TaskState<void>> state) noexcept : m_state(std::move(state)) {}

    FallibleTask(FallibleTask&&) noexcept            = default;
    FallibleTask& operator=(FallibleTask&&) noexcept = default;
    FallibleTask(const FallibleTask&)                = delete;
    FallibleTask& operator=(const FallibleTask&)     = delete;

    ~FallibleTask() { close(); }

    [[nodiscard]] auto cancel() && {
        auto state = std::exchange(m_state, nullptr);
        if (state) state->mark_closed();
        struct awaiter {
            std::shared_ptr<internal::TaskState<void>> state;
            bool await_ready() const noexcept {
                return !state || internal::terminal(state->flags.load(std::memory_order_acquire));
            }
            void await_suspend(std::coroutine_handle<> h) {
                if (state)
                    state->wait_or_resume(h);
                else
                    h.resume();
            }
            bool await_resume() {
                if (!state || state->exception) return false;
                auto flags = state->flags.load(std::memory_order_acquire);
                return (flags & internal::Completed) != 0;
            }
        };
        return awaiter{std::move(state)};
    }

    [[nodiscard]] FallibleTask fallible() && { return std::move(*this); }

    void detach() noexcept { m_state.reset(); }

    [[nodiscard]] bool is_finished() const noexcept {
        return !m_state || internal::terminal(m_state->flags.load(std::memory_order_acquire));
    }

    explicit operator bool() const noexcept { return m_state != nullptr; }

    FallibleTask& operator co_await() & noexcept { return *this; }
    FallibleTask&& operator co_await() && noexcept { return std::move(*this); }

    [[nodiscard]] bool await_ready() const noexcept { return is_finished(); }
    void await_suspend(std::coroutine_handle<> h) {
        if (m_state)
            m_state->wait_or_resume(h);
        else
            h.resume();
    }
    bool await_resume() {
        if (!m_state || m_state->exception) return false;
        auto flags = m_state->flags.load(std::memory_order_acquire);
        return (flags & internal::Completed) != 0;
    }
};

template <typename T>
FallibleTask<T> Task<T>::fallible() && {
    return FallibleTask<T>(std::exchange(m_state, nullptr));
}

inline FallibleTask<void> Task<void>::fallible() && { return FallibleTask<void>(std::exchange(m_state, nullptr)); }

EPIX_EXPORT template <typename S>
    requires STDEXEC::sender<std::decay_t<S>> && (!std::invocable<std::decay_t<S>&>)
[[nodiscard]] auto spawn(S&& sender) {
    using Sender       = std::decay_t<S>;
    using T            = internal::sender_task_value_t<Sender>;
    auto [task, state] = Task<T>::make();

    auto mark_error = [state](auto&& error) noexcept {
        state->fail(internal::to_exception_ptr(std::forward<decltype(error)>(error)));
    };

    auto mark_stopped = [state]() noexcept { state->mark_closed(); };

    if constexpr (std::is_void_v<T>) {
        auto monitored = std::forward<S>(sender) | STDEXEC::then([state]() mutable { state->complete(); }) |
                         STDEXEC::upon_error(std::move(mark_error)) | STDEXEC::upon_stopped(std::move(mark_stopped));
        if constexpr (internal::is_stdexec_task_v<Sender>)
            exec::start_detached(std::move(monitored),
                                 STDEXEC::prop{STDEXEC::get_start_scheduler, STDEXEC::inline_scheduler{}});
        else
            exec::start_detached(std::move(monitored));
    } else {
        auto monitored = std::forward<S>(sender) |
                         STDEXEC::then([state](T value) mutable { state->complete(std::move(value)); }) |
                         STDEXEC::upon_error(std::move(mark_error)) | STDEXEC::upon_stopped(std::move(mark_stopped));
        if constexpr (internal::is_stdexec_task_v<Sender>)
            exec::start_detached(std::move(monitored),
                                 STDEXEC::prop{STDEXEC::get_start_scheduler, STDEXEC::inline_scheduler{}});
        else
            exec::start_detached(std::move(monitored));
    }
    return std::move(task);
}

EPIX_EXPORT template <typename F, typename S>
    requires std::invocable<F> && std::invocable<S, Runnable, ScheduleInfo> && (!STDEXEC::sender<std::decay_t<F>>)
[[nodiscard]] auto spawn(F&& work, S&& schedule) {
    using T    = std::invoke_result_t<F>;
    auto state = std::make_shared<internal::TaskState<T>>();

    state->schedule_fn = std::move_only_function<void(std::shared_ptr<internal::TaskHeader>, ScheduleInfo)>(
        [s = std::forward<S>(schedule)](std::shared_ptr<internal::TaskHeader> state, ScheduleInfo info) mutable {
            std::invoke(s, Runnable(std::move(state)), info);
        });

    if constexpr (std::is_void_v<T>) {
        state->work =
            std::move_only_function<void()>([f = std::forward<F>(work)]() mutable { std::invoke(std::move(f)); });
    } else {
        state->work = std::move_only_function<T()>(
            [f = std::forward<F>(work)]() mutable -> T { return std::invoke(std::move(f)); });
    }

    std::shared_ptr<internal::TaskHeader> base = state;
    return std::pair<Runnable, Task<T>>(Runnable(std::move(base)), Task<T>(std::move(state)));
}

}  // namespace epix::async_task

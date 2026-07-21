#pragma once

// ── C++23 adaptation of Bevy's task_pool.rs ────────────────────────────────
//
// Reference: https://github.com/bevyengine/bevy/blob/main/crates/bevy_tasks/src/task_pool.rs

#ifndef EPIX_CXX_MODULE
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <epix/async_task.hpp>
#include <epix/common.hpp>
#include <exec/asio/asio_thread_pool.hpp>
#include <exec/start_detached.hpp>
#include <exec/static_thread_pool.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stdexec/execution.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#endif
#include <epix/task/thread_executor.hpp>

namespace epix::task {

EPIX_EXPORT struct TaskPool;
EPIX_EXPORT struct TaskPoolBuilder;
EPIX_EXPORT template <typename T>
struct Scope;

EPIX_EXPORT using AsioExecutor = decltype(std::declval<exec::asio::asio_thread_pool const&>().get_executor());

// ── Internal: backend bundles ──────────────────────────────────────────────

namespace internal {

struct StaticThreadPoolBackend {
    exec::static_thread_pool pool;
    size_t n;

    explicit StaticThreadPoolBackend(size_t thread_count)
        : pool(static_cast<std::uint32_t>(thread_count)), n(thread_count) {}

    [[nodiscard]] size_t thread_count() const noexcept { return n; }

    void enqueue(std::function<void()> fn) {
        exec::start_detached(STDEXEC::schedule(pool.get_scheduler()) |
                             STDEXEC::then([fn = std::move(fn)]() mutable { fn(); }));
    }
};

struct AsioThreadPoolBackend {
    exec::asio::asio_thread_pool pool;
    size_t n;

    explicit AsioThreadPoolBackend(size_t thread_count)
        : pool(static_cast<std::uint32_t>(thread_count)), n(thread_count) {}

    [[nodiscard]] size_t thread_count() const noexcept { return n; }

    void enqueue(std::function<void()> fn) {
        exec::start_detached(STDEXEC::schedule(pool.get_scheduler()) |
                             STDEXEC::then([fn = std::move(fn)]() mutable { fn(); }));
    }

    [[nodiscard]] std::optional<AsioExecutor> asio_executor() const { return pool.get_executor(); }
};

struct CallOnDrop {
    std::function<void()> fn;
    explicit CallOnDrop(std::function<void()> f) : fn(std::move(f)) {}
    ~CallOnDrop() {
        if (fn) fn();
    }
    CallOnDrop(const CallOnDrop&)            = delete;
    CallOnDrop& operator=(const CallOnDrop&) = delete;
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
    static_assert(always_false<T>::value, "epix::task::TaskPool::spawn supports senders with zero or one value");
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

template <typename T>
struct is_stdexec_task : std::false_type {};

template <typename T, typename Env>
struct is_stdexec_task<STDEXEC::task<T, Env>> : std::true_type {};

template <typename T>
inline constexpr bool is_stdexec_task_v = is_stdexec_task<std::remove_cvref_t<T>>::value;

template <typename Sender>
using sender_task_value_t = typename sender_task_value<std::remove_cvref_t<Sender>>::type;

}  // namespace internal

// ── available_parallelism ──────────────────────────────────────────────────

inline size_t available_parallelism() noexcept {
    auto n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
}

/**
 * @brief Selects the internal threading backend for TaskPool.
 *  - StaticThreadPool : stdexec static thread pool for CPU work.
 *  - AsioThreadPool   : stdexec Asio thread pool for I/O-capable sender work.
 *
 * ThreadPool and IoContext are kept as source-compatible aliases.
 */
EPIX_EXPORT enum class TaskPoolBackend {
    StaticThreadPool,
    AsioThreadPool,
    ThreadPool = StaticThreadPool,
    IoContext  = AsioThreadPool,
};

// ── TaskPoolBuilder ────────────────────────────────────────────────────────

EPIX_EXPORT struct TaskPoolBuilder {
    std::optional<size_t> m_num_threads;
    std::optional<std::string> m_thread_name;
    std::function<void()> m_on_thread_spawn;
    std::function<void()> m_on_thread_destroy;
    std::optional<TaskPoolBackend> m_backend;

    TaskPoolBuilder() noexcept = default;

    /** @brief Override the thread count. */
    TaskPoolBuilder& num_threads(size_t n) noexcept {
        m_num_threads = n;
        return *this;
    }

    /** @brief Source-compatible no-op with stdexec-backed pools. */
    TaskPoolBuilder& thread_name(std::string name) {
        m_thread_name = std::move(name);
        return *this;
    }

    /** @brief Source-compatible no-op with stdexec-backed pools. */
    TaskPoolBuilder& on_thread_spawn(std::function<void()> f) {
        m_on_thread_spawn = std::move(f);
        return *this;
    }

    /** @brief Source-compatible no-op with stdexec-backed pools. */
    TaskPoolBuilder& on_thread_destroy(std::function<void()> f) {
        m_on_thread_destroy = std::move(f);
        return *this;
    }

    /** @brief Explicitly select the backend (overrides auto-detection). */
    TaskPoolBuilder& backend(TaskPoolBackend b) noexcept {
        m_backend = b;
        return *this;
    }

    /** @brief Build the TaskPool. */
    TaskPool build();
};

// ── TaskPool ───────────────────────────────────────────────────────────────

EPIX_EXPORT struct TaskPool {
    TaskPool();
    explicit TaskPool(TaskPoolBuilder builder);
    ~TaskPool();

    TaskPool(TaskPool&&) noexcept        = default;
    TaskPool& operator=(TaskPool&&)      = default;
    TaskPool(const TaskPool&)            = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    [[nodiscard]] size_t thread_num() const noexcept { return m_thread_count; }

    [[nodiscard]] std::optional<AsioExecutor> try_get_asio_executor() const;

    [[nodiscard]] AsioExecutor get_asio_executor() const;

    template <typename F>
    decltype(auto) with_scheduler(F&& f) {
        if (m_backend_kind == TaskPoolBackend::StaticThreadPool) {
            auto backend = std::static_pointer_cast<internal::StaticThreadPoolBackend>(m_backend);
            return std::invoke(std::forward<F>(f), backend->pool.get_scheduler());
        }
        if (m_backend_kind == TaskPoolBackend::AsioThreadPool) {
            auto backend = std::static_pointer_cast<internal::AsioThreadPoolBackend>(m_backend);
            return std::invoke(std::forward<F>(f), backend->pool.get_scheduler());
        }
        throw std::logic_error("TaskPool has no scheduler backend");
    }

    // ── spawn ─────────────────────────────────────────────────────────

    template <typename S>
        requires STDEXEC::sender<std::decay_t<S>> && (!std::invocable<std::decay_t<S>&>)
    [[nodiscard]] auto spawn(S&& sender) {
        using Sender = std::decay_t<S>;
        using T      = internal::sender_task_value_t<Sender>;
        Sender snd(std::forward<S>(sender));

        if (m_backend_kind == TaskPoolBackend::StaticThreadPool) {
            auto backend = std::static_pointer_cast<internal::StaticThreadPoolBackend>(m_backend);
            if constexpr (internal::is_stdexec_task_v<Sender>)
                return async_task::spawn(
                    STDEXEC::starts_on(backend->pool.get_scheduler(), STDEXEC::just()) |
                    STDEXEC::let_value([snd = std::move(snd)]() mutable { return std::move(snd); }));
            else
                return async_task::spawn(STDEXEC::starts_on(backend->pool.get_scheduler(), std::move(snd)));
        }
        if (m_backend_kind == TaskPoolBackend::AsioThreadPool) {
            auto backend = std::static_pointer_cast<internal::AsioThreadPoolBackend>(m_backend);
            if constexpr (internal::is_stdexec_task_v<Sender>)
                return async_task::spawn(
                    STDEXEC::starts_on(backend->pool.get_scheduler(), STDEXEC::just()) |
                    STDEXEC::let_value([snd = std::move(snd)]() mutable { return std::move(snd); }));
            else
                return async_task::spawn(STDEXEC::starts_on(backend->pool.get_scheduler(), std::move(snd)));
        }
        return async_task::Task<T>{};
    }

    template <typename F>
        requires std::invocable<F> && std::move_constructible<F> && (!STDEXEC::sender<std::decay_t<F>>)
    [[nodiscard]] auto spawn(F&& work) {
        auto [runnable, task] =
            async_task::spawn(std::forward<F>(work), [backend = std::weak_ptr<void>(m_backend), kind = m_backend_kind](
                                                         async_task::Runnable r, async_task::ScheduleInfo) {
                auto runner = std::make_shared<async_task::Runnable>(std::move(r));
                auto poll   = std::make_shared<std::function<void()>>();
                *poll       = [runner, backend, kind, poll]() {
                    if (runner->run())
                        TaskPool::enqueue_on(backend, kind, *poll);
                    else
                        *poll = nullptr;
                };
                TaskPool::enqueue_on(backend, kind, *poll);
            });
        runnable.schedule();
        return std::move(task);
    }

    // ── spawn_local ───────────────────────────────────────────────────

    template <typename F>
        requires std::invocable<F> && std::move_constructible<F>
    [[nodiscard]] auto spawn_local(F&& work) {
        if (!t_local_ctx) {
            t_local_ctx = std::make_unique<asio::io_context>();
            t_local_work.emplace(asio::make_work_guard(*t_local_ctx));
        }
        auto [runnable, task] = async_task::spawn(
            std::forward<F>(work), [ctx = t_local_ctx.get()](async_task::Runnable r, async_task::ScheduleInfo) {
                auto runner = std::make_shared<async_task::Runnable>(std::move(r));
                auto poll   = std::make_shared<std::function<void()>>();
                *poll       = [runner, ctx, poll]() {
                    if (runner->run())
                        asio::post(*ctx, *poll);
                    else
                        *poll = nullptr;
                };
                asio::post(*ctx, *poll);
            });
        runnable.schedule();
        return std::move(task);
    }

    void with_local_executor(std::function<void()> f) {
        if (!t_local_ctx) {
            t_local_ctx = std::make_unique<asio::io_context>();
            t_local_work.emplace(asio::make_work_guard(*t_local_ctx));
        }
        f();
        t_local_ctx->poll();
    }

    // ── scope ─────────────────────────────────────────────────────────

    template <typename T, typename F>
        requires std::invocable<F, Scope<T>&>
    [[nodiscard]] std::vector<T> scope(F&& f) {
        Scope<T> s{m_backend, m_backend_kind};
        std::forward<F>(f)(s);
        return s.collect_results();
    }

    template <typename T, typename F>
        requires std::invocable<F, Scope<T>&>
    [[nodiscard]] std::vector<T> scope_with_executor(bool /*tick_pool_executor*/,
                                                     ThreadExecutor* /*external_executor*/,
                                                     F&& f) {
        // Simplified: delegate to scope. Full executor ticking can be added later.
        return scope<T>(std::forward<F>(f));
    }

    [[nodiscard]] static ThreadExecutor& get_thread_executor() {
        static thread_local ThreadExecutor s_exec;
        return s_exec;
    }

   private:
    friend struct TaskPoolBuilder;
    template <typename T>
    friend struct Scope;

    std::shared_ptr<void> m_backend;
    TaskPoolBackend m_backend_kind = TaskPoolBackend::AsioThreadPool;
    size_t m_thread_count          = 0;

    static thread_local inline std::unique_ptr<asio::io_context> t_local_ctx;
    static thread_local inline std::optional<asio::executor_work_guard<asio::io_context::executor_type>> t_local_work;

    TaskPool(std::shared_ptr<void> backend, TaskPoolBackend kind, size_t n)
        : m_backend(std::move(backend)), m_backend_kind(kind), m_thread_count(n) {}

    void enqueue(std::function<void()> fn) const { enqueue_on(m_backend, m_backend_kind, std::move(fn)); }

    static void enqueue_on(std::weak_ptr<void> backend, TaskPoolBackend kind, std::function<void()> fn);

    static TaskPool make_static_thread_pool(size_t n);

    static TaskPool make_asio_thread_pool(size_t n);

    static TaskPool from_builder(const TaskPoolBuilder& b);
};

// ── Scope ──────────────────────────────────────────────────────────────────

EPIX_EXPORT template <typename T>
struct Scope {
    template <typename F>
        requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
    void spawn(F&& f) {
        size_t idx = m_index++;
        m_pending->fetch_add(1, std::memory_order_release);
        if (idx >= m_results->size()) m_results->resize(idx + 1);

        auto [runnable, task] = async_task::spawn(
            [fn = std::forward<F>(f), results = m_results, pending = m_pending, mtx = m_mtx, cv = m_cv,
             idx]() mutable -> T {
                T val = std::invoke(std::move(fn));
                {
                    std::unique_lock lock(*mtx);
                    (*results)[idx] = std::move(val);
                }
                pending->fetch_sub(1, std::memory_order_release);
                cv->notify_one();
                return val;
            },
            [backend = std::weak_ptr<void>(m_backend), kind = m_backend_kind](async_task::Runnable r,
                                                                              async_task::ScheduleInfo) {
                TaskPool::enqueue_on(backend, kind, [r = std::move(r)]() mutable { r.run(); });
            });
        runnable.schedule();
        task.detach();
    }

    std::vector<T> collect_results() {
        std::unique_lock lock(*m_mtx);
        m_cv->wait(lock, [&] { return m_pending->load(std::memory_order_acquire) == 0; });
        return std::move(*m_results);
    }

   private:
    friend struct TaskPool;

    explicit Scope(std::shared_ptr<void> backend, TaskPoolBackend kind)
        : m_backend(std::move(backend)),
          m_backend_kind(kind),
          m_results(std::make_shared<std::vector<T>>()),
          m_pending(std::make_shared<std::atomic<size_t>>(0)),
          m_mtx(std::make_shared<std::mutex>()),
          m_cv(std::make_shared<std::condition_variable>()) {}

    std::shared_ptr<void> m_backend;
    TaskPoolBackend m_backend_kind = TaskPoolBackend::AsioThreadPool;
    std::shared_ptr<std::vector<T>> m_results;
    std::shared_ptr<std::atomic<size_t>> m_pending;
    std::shared_ptr<std::mutex> m_mtx;
    std::shared_ptr<std::condition_variable> m_cv;
    size_t m_index = 0;
};

// ── TaskPoolBuilder::build() and constructors ──────────────────────────────

}  // namespace epix::task

#pragma once

// ── C++23 adaptation of Bevy's task_pool.rs ────────────────────────────────
//
// Reference: https://github.com/bevyengine/bevy/blob/main/crates/bevy_tasks/src/task_pool.rs

#ifndef EPIX_CXX_MODULE
#include <asio/any_io_executor.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <atomic>
#include <condition_variable>
#include <epix/async_task.hpp>
#include <epix/common.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#endif
#include <epix/tasks/thread_executor.hpp>

namespace epix::tasks {

EPIX_EXPORT struct TaskPool;
EPIX_EXPORT struct TaskPoolBuilder;
EPIX_EXPORT template <typename T>
struct Scope;

// ── Internal: backend bundles ──────────────────────────────────────────────

namespace internal {

struct ThreadPoolBundle {
    asio::thread_pool pool;
    explicit ThreadPoolBundle(size_t n) : pool(n) {}
    ~ThreadPoolBundle() { pool.wait(); }
    ThreadPoolBundle(const ThreadPoolBundle&)            = delete;
    ThreadPoolBundle& operator=(const ThreadPoolBundle&) = delete;
};

struct IoBundle {
    asio::io_context ioc;
    asio::executor_work_guard<asio::io_context::executor_type> work{asio::make_work_guard(ioc)};
    std::vector<std::thread> threads;

    IoBundle() = default;
    ~IoBundle() {
        work.reset();
        for (auto& t : threads)
            if (t.joinable()) t.join();
    }
    IoBundle(const IoBundle&)            = delete;
    IoBundle& operator=(const IoBundle&) = delete;
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

}  // namespace internal

// ── available_parallelism ──────────────────────────────────────────────────

inline size_t available_parallelism() noexcept {
    auto n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
}

// ── TaskPoolBuilder ────────────────────────────────────────────────────────

EPIX_EXPORT struct TaskPoolBuilder {
    std::optional<size_t> m_num_threads;
    std::optional<std::string> m_thread_name;
    std::function<void()> m_on_thread_spawn;
    std::function<void()> m_on_thread_destroy;

    TaskPoolBuilder() noexcept = default;

    TaskPoolBuilder& num_threads(size_t n) noexcept {
        m_num_threads = n;
        return *this;
    }
    TaskPoolBuilder& thread_name(std::string name) {
        m_thread_name = std::move(name);
        return *this;
    }
    TaskPoolBuilder& on_thread_spawn(std::function<void()> f) {
        m_on_thread_spawn = std::move(f);
        return *this;
    }
    TaskPoolBuilder& on_thread_destroy(std::function<void()> f) {
        m_on_thread_destroy = std::move(f);
        return *this;
    }

    TaskPool build();
};

// ── TaskPool ───────────────────────────────────────────────────────────────

EPIX_EXPORT struct TaskPool {
    TaskPool();
    explicit TaskPool(TaskPoolBuilder builder);
    ~TaskPool() = default;

    TaskPool(TaskPool&&) noexcept        = default;
    TaskPool& operator=(TaskPool&&)      = default;
    TaskPool(const TaskPool&)            = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    [[nodiscard]] size_t thread_num() const noexcept { return m_thread_count; }

    // ── spawn ─────────────────────────────────────────────────────────

    template <typename F>
        requires std::invocable<F> && std::move_constructible<F>
    [[nodiscard]] auto spawn(F&& work) -> async_task::Task<std::invoke_result_t<F>> {
        auto [runnable, task] = async_task::spawn(std::forward<F>(work),
                                                  [ex = m_executor](async_task::Runnable r, async_task::ScheduleInfo) {
                                                      asio::post(ex, [r = std::move(r)]() mutable { r.run(); });
                                                  });
        runnable.schedule();
        return std::move(task);
    }

    // ── spawn_local ───────────────────────────────────────────────────

    template <typename F>
        requires std::invocable<F> && std::move_constructible<F>
    [[nodiscard]] auto spawn_local(F&& work) -> async_task::Task<std::invoke_result_t<F>> {
        if (!t_local_ctx) {
            t_local_ctx = std::make_unique<asio::io_context>();
            t_local_work.emplace(asio::make_work_guard(*t_local_ctx));
        }
        auto [runnable, task] = async_task::spawn(
            std::forward<F>(work), [ctx = t_local_ctx.get()](async_task::Runnable r, async_task::ScheduleInfo) {
                asio::post(*ctx, [r = std::move(r)]() mutable { r.run(); });
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
        Scope<T> s{m_executor};
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
    asio::any_io_executor m_executor;
    size_t m_thread_count = 0;

    static thread_local inline std::unique_ptr<asio::io_context> t_local_ctx;
    static thread_local inline std::optional<asio::executor_work_guard<asio::io_context::executor_type>> t_local_work;

    TaskPool(std::shared_ptr<void> backend, asio::any_io_executor ex, size_t n)
        : m_backend(std::move(backend)), m_executor(std::move(ex)), m_thread_count(n) {}

    static TaskPool make_thread_pool(size_t n) {
        auto bundle = std::make_shared<internal::ThreadPoolBundle>(n);
        auto ex     = bundle->pool.get_executor();
        return {std::move(bundle), std::move(ex), n};
    }

    static TaskPool make_io_context(size_t n,
                                    std::optional<std::string> thread_name,
                                    std::function<void()> on_spawn,
                                    std::function<void()> on_destroy) {
        auto bundle = std::make_shared<internal::IoBundle>();
        bundle->threads.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            bundle->threads.emplace_back([b = bundle.get(), thread_name, on_spawn, on_destroy, i] {
                auto name = thread_name ? *thread_name + " (" + std::to_string(i) + ")"
                                        : std::string("TaskPool (") + std::to_string(i) + ")";
                // Thread naming is platform-specific; best-effort via standard means.
                if (on_spawn) on_spawn();
                internal::CallOnDrop _destructor{on_destroy};
                b->ioc.run();
            });
        }
        auto ex = bundle->ioc.get_executor();
        return {std::move(bundle), std::move(ex), n};
    }

    static TaskPool from_builder(const TaskPoolBuilder& b) {
        size_t n      = b.m_num_threads.value_or(available_parallelism());
        bool need_ioc = b.m_thread_name.has_value() || static_cast<bool>(b.m_on_thread_spawn) ||
                        static_cast<bool>(b.m_on_thread_destroy);
        if (need_ioc)
            return make_io_context(n, b.m_thread_name, b.m_on_thread_spawn, b.m_on_thread_destroy);
        else
            return make_thread_pool(n);
    }
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
            [ex = m_executor](async_task::Runnable r, async_task::ScheduleInfo) {
                asio::post(ex, [r = std::move(r)]() mutable { r.run(); });
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

    explicit Scope(asio::any_io_executor ex)
        : m_executor(std::move(ex)),
          m_results(std::make_shared<std::vector<T>>()),
          m_pending(std::make_shared<std::atomic<size_t>>(0)),
          m_mtx(std::make_shared<std::mutex>()),
          m_cv(std::make_shared<std::condition_variable>()) {}

    asio::any_io_executor m_executor;
    std::shared_ptr<std::vector<T>> m_results;
    std::shared_ptr<std::atomic<size_t>> m_pending;
    std::shared_ptr<std::mutex> m_mtx;
    std::shared_ptr<std::condition_variable> m_cv;
    size_t m_index = 0;
};

// ── TaskPoolBuilder::build() and constructors ──────────────────────────────

inline TaskPool TaskPoolBuilder::build() { return TaskPool::from_builder(*this); }

inline TaskPool::TaskPool() : TaskPool(make_thread_pool(available_parallelism())) {}

inline TaskPool::TaskPool(TaskPoolBuilder builder) : TaskPool(from_builder(builder)) {}

}  // namespace epix::tasks

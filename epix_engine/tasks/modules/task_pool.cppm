module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#endif
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>

export module epix.tasks:task_pool;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import BS.thread_pool;
import :task;

namespace epix::tasks {

/**
 * @brief Gets the logical CPU core count available to the current process.
 * Identical to `std::thread::hardware_concurrency()`, except it will
 * return a default value of 1 if it returns 0.
 * Matches `bevy_tasks::available_parallelism`.
 */
export inline std::size_t available_parallelism() {
    auto n = std::thread::hardware_concurrency();
    return n > 0 ? n : 1;
}

export struct TaskPool;
export struct TaskPoolBuilder;

/**
 * @brief Selects the internal threading backend for TaskPool.
 *  - ThreadPool : wraps asio::thread_pool — simplest, no thread customisation.
 *  - IoContext  : wraps asio::io_context with manually managed threads —
 *                 supports thread names and spawn/destroy callbacks.
 */
export enum class TaskPoolBackend { ThreadPool, IoContext };

// ─── Submit helpers ───────────────────────────────────────────────────────────

template <typename T, typename F>
void submit_to_state(const asio::any_io_executor& ex, std::shared_ptr<detail::TaskState<T>> state, F f) {
    asio::post(ex, [s = std::move(state), fn = std::move(f)]() mutable {
        try {
            s->set_value(fn());
        } catch (...) {
            s->set_exception(std::current_exception());
        }
    });
}

template <typename F>
void submit_to_state_void(const asio::any_io_executor& ex, std::shared_ptr<detail::TaskState<void>> state, F f) {
    asio::post(ex, [s = std::move(state), fn = std::move(f)]() mutable {
        try {
            fn();
            s->set_done();
        } catch (...) {
            s->set_exception(std::current_exception());
        }
    });
}

// ─── CallOnDrop ──────────────────────────────────────────────────────────────
// RAII guard that invokes a callback on destruction. Used to ensure
// on_thread_destroy runs even if the thread panics.
// Matches bevy's `CallOnDrop` in task_pool.rs.
struct CallOnDrop {
    std::function<void()> m_fn;
    explicit CallOnDrop(std::function<void()> f) : m_fn(std::move(f)) {}
    ~CallOnDrop() {
        if (m_fn) m_fn();
    }
    CallOnDrop(const CallOnDrop&)            = delete;
    CallOnDrop& operator=(const CallOnDrop&) = delete;
};

// ─── Backend bundles ─────────────────────────────────────────────────────────
// Each bundle owns the backend resource and drains/joins in its destructor.
// TaskPool stores a shared_ptr<void> for type-erased lifetime ownership and
// a separate any_io_executor for fast dispatch.

// ThreadPoolBundle: wraps asio::thread_pool.
// ~asio::thread_pool() calls stop()+join(), skipping queued work.
// We call wait() first to drain all pending handlers before destruction.
struct ThreadPoolBundle {
    asio::thread_pool pool;
    explicit ThreadPoolBundle(std::size_t n) : pool(n) {}
    ~ThreadPoolBundle() { pool.wait(); }
    ThreadPoolBundle(const ThreadPoolBundle&)            = delete;
    ThreadPoolBundle& operator=(const ThreadPoolBundle&) = delete;
};

// IoBundle: wraps asio::io_context with manually managed threads.
// Destructor resets the work guard (allows ioc to drain) then joins threads.
struct IoBundle {
    asio::io_context ioc;
    asio::executor_work_guard<asio::io_context::executor_type> work{asio::make_work_guard(ioc)};
    std::vector<std::thread> threads;

    IoBundle()                           = default;
    IoBundle(const IoBundle&)            = delete;
    IoBundle& operator=(const IoBundle&) = delete;

    ~IoBundle() {
        work.reset();
        for (auto& t : threads)
            if (t.joinable()) t.join();
    }
};

// ─── Scope<T> ────────────────────────────────────────────────────────────────

/**
 * @brief A scope that allows spawning non-static tasks.
 * All tasks spawned in the scope are awaited before scope() returns.
 * Matches `bevy_tasks::Scope`.
 */
export template <typename T>
struct Scope {
   private:
    asio::any_io_executor m_executor;
    std::vector<Task<T>> m_tasks;

   public:
    explicit Scope(asio::any_io_executor ex) : m_executor(std::move(ex)) {}

    /** @brief Spawn a callable returning T on the pool. */
    template <typename F>
        requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
    void spawn(F&& f) {
        auto [task, state] = Task<T>::make();
        submit_to_state(m_executor, std::move(state), std::forward<F>(f));
        m_tasks.push_back(std::move(task));
    }

    /** @brief Spawn on the scope thread — same semantics here. */
    template <typename F>
        requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
    void spawn_on_scope(F&& f) {
        spawn(std::forward<F>(f));
    }

    /** @brief Spawn on the external thread executor.
     *  Matches `bevy_tasks::Scope::spawn_on_external`.
     *  Currently delegates to spawn (no separate external executor). */
    template <typename F>
        requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
    void spawn_on_external(F&& f) {
        spawn(std::forward<F>(f));
    }

    /** @brief Internal: block-wait all tasks and collect results. */
    std::vector<T> collect_results() {
        std::vector<T> results;
        results.reserve(m_tasks.size());
        for (auto& t : m_tasks) {
            auto v = t.block();
            if (v) results.push_back(std::move(*v));
        }
        return results;
    }
};

// ─── TaskPoolBuilder ──────────────────────────────────────────────────────────

/**
 * @brief Builder for TaskPool — configure threads, name, callbacks, and backend.
 * Matches `bevy_tasks::TaskPoolBuilder`.
 */
export struct TaskPoolBuilder {
    std::optional<std::size_t> m_num_threads;
    std::optional<std::size_t> m_stack_size;
    std::optional<std::string> m_thread_name;
    std::function<void()> m_on_thread_spawn;
    std::function<void()> m_on_thread_destroy;
    std::optional<TaskPoolBackend> m_backend;

    TaskPoolBuilder() = default;

    /** @brief Override the thread count. */
    TaskPoolBuilder& num_threads(std::size_t n) {
        m_num_threads = n;
        return *this;
    }

    /** @brief Override the stack size of threads created for the pool. */
    TaskPoolBuilder& stack_size(std::size_t n) {
        m_stack_size = n;
        return *this;
    }

    /** @brief Set a prefix for worker thread names (forces IoContext backend). */
    TaskPoolBuilder& thread_name(std::string name) {
        m_thread_name = std::move(name);
        return *this;
    }

    /** @brief Callback invoked on each spawned worker thread (forces IoContext backend). */
    TaskPoolBuilder& on_thread_spawn(std::function<void()> f) {
        m_on_thread_spawn = std::move(f);
        return *this;
    }

    /** @brief Callback invoked when a worker thread exits (forces IoContext backend). */
    TaskPoolBuilder& on_thread_destroy(std::function<void()> f) {
        m_on_thread_destroy = std::move(f);
        return *this;
    }

    /** @brief Explicitly select the backend (overrides auto-detection). */
    TaskPoolBuilder& backend(TaskPoolBackend b) {
        m_backend = b;
        return *this;
    }

    /** @brief Build the TaskPool. */
    TaskPool build();
};

// ─── TaskPool ────────────────────────────────────────────────────────────────

/**
 * @brief A thread pool for executing tasks.
 *
 * Holds an `asio::any_io_executor` directly — no indirection struct.
 * Backed by either `asio::thread_pool` (default) or `asio::io_context` with
 * manually managed threads.
 *
 * Matches `bevy_tasks::TaskPool`.
 */
export struct TaskPool {
   private:
    // m_backend owns the lifetime of the underlying thread_pool or IoBundle.
    // any_io_executor does NOT extend that lifetime — it is merely a cached
    // reference for quick dispatch and must never outlive m_backend.
    std::shared_ptr<void> m_backend;   // shared_ptr<asio::thread_pool> or shared_ptr<IoBundle>
    asio::any_io_executor m_executor;  // non-owning view into m_backend
    std::size_t m_thread_count{0};

    TaskPool(std::shared_ptr<void> backend, asio::any_io_executor ex, std::size_t n)
        : m_backend(std::move(backend)), m_executor(std::move(ex)), m_thread_count(n) {}

    /** @brief Build with asio::thread_pool backend.
     *  ~ThreadPoolBundle() calls wait() to drain queued work before joining. */
    static TaskPool make_thread_pool(std::size_t n) {
        auto bundle = std::make_shared<ThreadPoolBundle>(n);
        auto ex     = bundle->pool.get_executor();
        return TaskPool{std::move(bundle), std::move(ex), n};
    }

    /** @brief Build with asio::io_context backend + full thread customisation.
     *  ~IoBundle() resets the work guard and joins all threads. */
    static TaskPool make_io_context(std::size_t n,
                                    std::optional<std::string> thread_name,
                                    std::function<void()> on_spawn,
                                    std::function<void()> on_destroy) {
        auto bundle = std::make_shared<IoBundle>();
        bundle->threads.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            bundle->threads.emplace_back([b = bundle.get(), thread_name, on_spawn, on_destroy, i]() {
                auto name = thread_name ? *thread_name + " (" + std::to_string(i) + ")"
                                        : std::string("TaskPool (") + std::to_string(i) + ")";
                BS::this_thread::set_os_thread_name(name);
                if (on_spawn) {
                    on_spawn();
                }
                CallOnDrop _destructor{on_destroy};
                b->ioc.run();
            });
        }
        auto ex = bundle->ioc.get_executor();
        return TaskPool{std::move(bundle), std::move(ex), n};
    }

   public:
    /** @brief Default constructor — uses available_parallelism() threads.
     *  Matches bevy's `TaskPool::new()` which calls `TaskPoolBuilder::new().build()`. */
    TaskPool() : TaskPool(make_thread_pool(available_parallelism())) {}

    /** @brief ThreadPool backend with explicit thread count. */
    explicit TaskPool(std::size_t n) : TaskPool(make_thread_pool(n)) {}

    /** @brief Explicit backend selection with thread count. */
    TaskPool(std::size_t n, TaskPoolBackend backend)
        : TaskPool(backend == TaskPoolBackend::IoContext ? make_io_context(n, std::nullopt, {}, {})
                                                         : make_thread_pool(n)) {}

    ~TaskPool()                          = default;  // m_backend shared_ptr dtor handles cleanup
    TaskPool(TaskPool&&) noexcept        = default;
    TaskPool& operator=(TaskPool&&)      = default;
    TaskPool(const TaskPool&)            = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    /** @brief Return number of worker threads. */
    std::size_t thread_num() const { return m_thread_count; }

    /** @brief The underlying type-erased executor. */
    const asio::any_io_executor& executor() const { return m_executor; }

    /** @brief Spawn a callable, returns Task<T> or Task<void>. */
    template <typename F>
        requires std::invocable<F> && (!std::is_void_v<std::invoke_result_t<F>>)
    auto spawn(F&& f) -> Task<std::invoke_result_t<F>> {
        using T            = std::invoke_result_t<F>;
        auto [task, state] = Task<T>::make();
        submit_to_state(m_executor, std::move(state), std::forward<F>(f));
        return std::move(task);
    }

    template <typename F>
        requires std::invocable<F> && std::is_void_v<std::invoke_result_t<F>>
    Task<void> spawn(F&& f) {
        auto [task, state] = Task<void>::make();
        submit_to_state_void(m_executor, std::move(state), std::forward<F>(f));
        return std::move(task);
    }

    /** @brief Fire-and-forget coroutine. */
    void spawn_detached(asio::awaitable<void> coro) { asio::co_spawn(m_executor, std::move(coro), asio::detached); }

    /** @brief Spawn `asio::awaitable<T>` coroutine, returns Task<T>. */
    template <typename T>
        requires(!std::is_void_v<T>)
    Task<T> spawn(asio::awaitable<T> coro) {
        auto [task, state] = Task<T>::make();
        asio::co_spawn(
            m_executor,
            [s = state, c = std::move(coro)]() mutable -> asio::awaitable<void> {
                try {
                    s->set_value(co_await std::move(c));
                } catch (...) {
                    s->set_exception(std::current_exception());
                }
            },
            asio::detached);
        return std::move(task);
    }

    /** @brief Spawn `asio::awaitable<void>` coroutine, returns Task<void>. */
    Task<void> spawn(asio::awaitable<void> coro) {
        auto [task, state] = Task<void>::make();
        asio::co_spawn(
            m_executor,
            [s = state, c = std::move(coro)]() mutable -> asio::awaitable<void> {
                try {
                    co_await std::move(c);
                    s->set_done();
                } catch (...) {
                    s->set_exception(std::current_exception());
                }
            },
            asio::detached);
        return std::move(task);
    }

    /** @brief Run a scope function, wait for all spawned tasks. */
    template <typename T, typename F>
        requires std::invocable<F, Scope<T>&>
    std::vector<T> scope(F&& f) {
        Scope<T> s{m_executor};
        std::forward<F>(f)(s);
        return s.collect_results();
    }

    /** @brief Build from a TaskPoolBuilder. */
    static TaskPool from_builder(const TaskPoolBuilder& b) {
        std::size_t n  = b.m_num_threads.value_or(available_parallelism());
        bool needs_ioc = b.m_thread_name.has_value() || static_cast<bool>(b.m_on_thread_spawn) ||
                         static_cast<bool>(b.m_on_thread_destroy);
        auto chosen    = b.m_backend.value_or(needs_ioc ? TaskPoolBackend::IoContext : TaskPoolBackend::ThreadPool);

        if (chosen == TaskPoolBackend::IoContext)
            return make_io_context(n, b.m_thread_name, b.m_on_thread_spawn, b.m_on_thread_destroy);
        else
            return make_thread_pool(n);
    }
};

inline TaskPool TaskPoolBuilder::build() { return TaskPool::from_builder(*this); }

}  // namespace epix::tasks

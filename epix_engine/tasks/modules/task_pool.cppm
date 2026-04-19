module;

#include <exec/start_detached.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

export module epix.tasks:task_pool;

import std;
import :task;

namespace epix::tasks {

export struct TaskPool;
export struct TaskPoolBuilder;

/**
 * @brief Helper: submit F on scheduler, fulfill state on completion.
 */
template <typename T, typename F>
void submit_to_state(auto scheduler, std::shared_ptr<detail::TaskState<T>> state, F f) {
    auto snd = stdexec::schedule(scheduler) | stdexec::then([s = std::move(state), fn = std::move(f)]() mutable {
                   try {
                       s->set_value(fn());
                   } catch (...) {
                       s->set_exception(std::current_exception());
                   }
               });
    exec::start_detached(std::move(snd));
}

template <typename F>
void submit_to_state_void(auto scheduler, std::shared_ptr<detail::TaskState<void>> state, F f) {
    auto snd = stdexec::schedule(scheduler) | stdexec::then([s = std::move(state), fn = std::move(f)]() mutable {
                   try {
                       fn();
                       s->set_done();
                   } catch (...) {
                       s->set_exception(std::current_exception());
                   }
               });
    exec::start_detached(std::move(snd));
}

/**
 * @brief A scope that allows spawning non-static tasks.
 * All tasks spawned in the scope are awaited before scope() returns.
 * Matches `bevy_tasks::Scope`.
 */
export template <typename T>
struct Scope {
   private:
    exec::static_thread_pool& m_pool;
    std::vector<Task<T>> m_tasks;

   public:
    explicit Scope(exec::static_thread_pool& pool) : m_pool(pool) {}

    /**
     * @brief Spawn a callable returning T on the pool.
     * Matches `Scope::spawn(async { ... })`.
     */
    template <typename F>
        requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
    void spawn(F&& f) {
        auto [task, state] = Task<T>::make();
        submit_to_state(m_pool.get_scheduler(), std::move(state), std::forward<F>(f));
        m_tasks.push_back(std::move(task));
    }

    /**
     * @brief Spawn on the scope thread — same semantics here.
     * Matches `Scope::spawn_on_scope`.
     */
    template <typename F>
        requires std::invocable<F> && std::convertible_to<std::invoke_result_t<F>, T>
    void spawn_on_scope(F&& f) {
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

/**
 * @brief Builder for TaskPool — configure threads, name, and callbacks.
 * Matches `bevy_tasks::TaskPoolBuilder`.
 */
export struct TaskPoolBuilder {
    std::optional<std::size_t> m_num_threads;
    std::optional<std::string> m_thread_name;
    std::function<void()> m_on_thread_spawn;
    std::function<void()> m_on_thread_destroy;

    TaskPoolBuilder() = default;

    /** @brief Override the thread count. Matches `TaskPoolBuilder::num_threads`. */
    TaskPoolBuilder& num_threads(std::size_t n) {
        m_num_threads = n;
        return *this;
    }

    /** @brief Override the thread name prefix. Matches `TaskPoolBuilder::thread_name`. */
    TaskPoolBuilder& thread_name(std::string name) {
        m_thread_name = std::move(name);
        return *this;
    }

    /** @brief Callback invoked on each spawned thread. Matches `TaskPoolBuilder::on_thread_spawn`. */
    TaskPoolBuilder& on_thread_spawn(std::function<void()> f) {
        m_on_thread_spawn = std::move(f);
        return *this;
    }

    /** @brief Callback invoked on each dying thread. Matches `TaskPoolBuilder::on_thread_destroy`. */
    TaskPoolBuilder& on_thread_destroy(std::function<void()> f) {
        m_on_thread_destroy = std::move(f);
        return *this;
    }

    /** @brief Build the TaskPool. Matches `TaskPoolBuilder::build`. */
    TaskPool build();
};

/**
 * @brief A thread pool for executing tasks.
 *
 * Wraps `exec::static_thread_pool` from stdexec.
 * Tasks spawned on the pool are eagerly started and can be awaited via Task<T>.
 * Matches `bevy_tasks::TaskPool`.
 */
export struct TaskPool {
   private:
    std::shared_ptr<exec::static_thread_pool> m_pool;
    std::size_t m_thread_count;

   public:
    /** @brief Default constructor — uses hardware_concurrency threads. */
    TaskPool()
        : m_pool(std::make_shared<exec::static_thread_pool>(std::thread::hardware_concurrency())),
          m_thread_count(std::thread::hardware_concurrency()) {}

    explicit TaskPool(std::size_t n_threads)
        : m_pool(std::make_shared<exec::static_thread_pool>(n_threads)), m_thread_count(n_threads) {}

    /** @brief Return number of worker threads. Matches `TaskPool::thread_num`. */
    std::size_t thread_num() const { return m_thread_count; }

    /** @brief Get the underlying scheduler. */
    auto get_scheduler() { return m_pool->get_scheduler(); }

    /**
     * @brief Spawn a callable on the pool, returns Task<T>.
     * For void callables returns Task<void>.
     * Matches `TaskPool::spawn`.
     */
    template <typename F>
        requires std::invocable<F> && (!std::is_void_v<std::invoke_result_t<F>>)
    auto spawn(F&& f) -> Task<std::invoke_result_t<F>> {
        using T            = std::invoke_result_t<F>;
        auto [task, state] = Task<T>::make();
        submit_to_state(m_pool->get_scheduler(), std::move(state), std::forward<F>(f));
        return std::move(task);
    }

    template <typename F>
        requires std::invocable<F> && std::is_void_v<std::invoke_result_t<F>>
    Task<void> spawn(F&& f) {
        auto [task, state] = Task<void>::make();
        submit_to_state_void(m_pool->get_scheduler(), std::move(state), std::forward<F>(f));
        return std::move(task);
    }

    // -------------------------------------------------------------------------
    // exec::task<T> coroutine spawn overloads
    //
    // Schedules an stdexec coroutine on this pool and bridges its completion
    // into a Task<T> (or void fire-and-forget).  Callers never need to obtain
    // the scheduler explicitly — the pool owns the execution context.
    //
    // These match the intent of Bevy's `TaskPool::spawn(async move { ... })`.
    // -------------------------------------------------------------------------

    /** @brief Schedule an `exec::task<T>` coroutine on this pool; returns Task<T>.
     *  On error or cancellation the Task's future will store the exception. */
    template <typename T>
        requires(!std::is_void_v<T>)
    Task<T> spawn(exec::task<T> coro) {
        auto [task, state] = Task<T>::make();
        auto snd           = stdexec::on(m_pool->get_scheduler(), std::move(coro)) |
                             stdexec::then([s = state](T val) mutable { s->set_value(std::move(val)); }) |
                             stdexec::upon_error([s = state](auto&& err) mutable {
                       if constexpr (std::same_as<std::decay_t<decltype(err)>, std::exception_ptr>) {
                           s->set_exception(err);
                       } else {
                           s->set_exception(std::make_exception_ptr(err));
                       }
                             });
        exec::start_detached(std::move(snd));
        return std::move(task);
    }

    /** @brief Schedule an `exec::task<void>` coroutine on this pool; returns Task<void>. */
    Task<void> spawn(exec::task<void> coro) {
        auto [task, state] = Task<void>::make();
        auto snd           = stdexec::on(m_pool->get_scheduler(), std::move(coro)) |
                             stdexec::then([s = state]() mutable { s->set_done(); }) |
                             stdexec::upon_error([s = state](auto&& err) mutable {
                       if constexpr (std::same_as<std::decay_t<decltype(err)>, std::exception_ptr>) {
                           s->set_exception(err);
                       } else {
                           s->set_exception(std::make_exception_ptr(err));
                       }
                             });
        exec::start_detached(std::move(snd));
        return std::move(task);
    }

    /** @brief Fire-and-forget: schedule an `exec::task<T>` on this pool, discard result.
     *  Matches `TaskPool::spawn_detached` / Bevy's fire-and-forget task spawn. */
    template <typename T>
    void spawn_detached(exec::task<T> coro) {
        exec::start_detached(stdexec::on(m_pool->get_scheduler(), std::move(coro)));
    }

    /**
     * @brief Run a scope function that can spawn tasks; wait for all spawned tasks.
     * Returns a Vec<T> of all results in spawn order (if spawned in order).
     * Matches `TaskPool::scope(|s| { ... })`.
     */
    template <typename T, typename F>
        requires std::invocable<F, Scope<T>&>
    std::vector<T> scope(F&& f) {
        Scope<T> s{*m_pool};
        std::forward<F>(f)(s);
        return s.collect_results();
    }

    /** @brief Create a TaskPool from a builder. */
    static TaskPool from_builder(const TaskPoolBuilder& b) {
        std::size_t n = b.m_num_threads.value_or(std::thread::hardware_concurrency());
        return TaskPool{n};
    }
};

inline TaskPool TaskPoolBuilder::build() { return TaskPool::from_builder(*this); }

}  // namespace epix::tasks

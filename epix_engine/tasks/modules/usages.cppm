module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>
#endif
export module epix.tasks:usages;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :task_pool;

namespace epix::tasks {

/**
 * @brief Macro-equivalent for global singleton task pool newtypes.
 *
 * Each pool type is a distinct type wrapping a TaskPool with a global
 * once-initialised instance. Matches bevy_tasks usages.rs pattern.
 */

#define EPIX_DEFINE_TASK_POOL(Name)                                                       \
    export struct Name {                                                                  \
       private:                                                                           \
        TaskPool m_pool;                                                                  \
        explicit Name(TaskPool p) : m_pool(std::move(p)) {}                               \
        static Name*& global_ptr() {                                                      \
            static Name* ptr = nullptr;                                                   \
            return ptr;                                                                   \
        }                                                                                 \
                                                                                          \
       public:                                                                            \
        /** @brief Initialize (or return) the global instance. */                         \
        static Name& get_or_init(std::function<TaskPool()> f) {                           \
            static std::once_flag flag;                                                   \
            std::call_once(flag, [&]() { global_ptr() = new Name(f()); });                \
            return *global_ptr();                                                         \
        }                                                                                 \
        /** @brief Return the global instance or nullptr if not initialized. */           \
        static Name* try_get() { return global_ptr(); }                                   \
        /** @brief Return the global instance. Panics (terminates) if not initialized. */ \
        static Name& get() {                                                              \
            auto* p = global_ptr();                                                       \
            if (!p) std::terminate();                                                     \
            return *p;                                                                    \
        }                                                                                 \
        TaskPool& pool() { return m_pool; }                                               \
        const TaskPool& pool() const { return m_pool; }                                   \
        /** @brief Delegate spawn to inner pool. */                                       \
        template <typename F>                                                             \
        auto spawn(F&& f) {                                                               \
            return m_pool.spawn(std::forward<F>(f));                                      \
        }                                                                                 \
        /** @brief Delegate scope to inner pool. */                                       \
        template <typename T, typename Fn>                                                \
        std::vector<T> scope(Fn&& fn) {                                                   \
            return m_pool.scope<T>(std::forward<Fn>(fn));                                 \
        }                                                                                 \
        std::size_t thread_num() const { return m_pool.thread_num(); }                    \
    }

EPIX_DEFINE_TASK_POOL(ComputeTaskPool);
EPIX_DEFINE_TASK_POOL(AsyncComputeTaskPool);
EPIX_DEFINE_TASK_POOL(IoTaskPool);

#undef EPIX_DEFINE_TASK_POOL

}  // namespace epix::tasks

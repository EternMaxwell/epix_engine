#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <mutex>
#include <utility>
#include <vector>
#endif
#include <epix/tasks/task_pool.hpp>

namespace epix::tasks {

#define EPIX_DEFINE_TASK_POOL(Name)                                                       \
    EPIX_EXPORT struct Name {                                                             \
       private:                                                                           \
        TaskPool m_pool;                                                                  \
        explicit Name(TaskPool p) : m_pool(std::move(p)) {}                               \
        static Name*& global_ptr() noexcept {                                             \
            static Name* ptr = nullptr;                                                   \
            return ptr;                                                                   \
        }                                                                                 \
                                                                                          \
       public:                                                                            \
        static Name& get_or_init(TaskPool pool) {                                         \
            static std::once_flag flag;                                                   \
            std::call_once(flag, [&]() { global_ptr() = new Name(std::move(pool)); });    \
            return *global_ptr();                                                         \
        }                                                                                 \
        template <typename F>                                                             \
            requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, TaskPool> \
        static Name& get_or_init(F&& f) {                                                 \
            return get_or_init(f());                                                      \
        }                                                                                 \
        static Name* try_get() noexcept { return global_ptr(); }                          \
        static Name& get() noexcept {                                                     \
            auto* p = global_ptr();                                                       \
            if (!p) std::terminate();                                                     \
            return *p;                                                                    \
        }                                                                                 \
        template <typename F>                                                             \
        auto spawn(F&& f) {                                                               \
            return m_pool.spawn(std::forward<F>(f));                                      \
        }                                                                                 \
        template <typename T, typename Fn>                                                \
        std::vector<T> scope(Fn&& fn) {                                                   \
            return m_pool.scope<T>(std::forward<Fn>(fn));                                 \
        }                                                                                 \
        size_t thread_num() const noexcept { return m_pool.thread_num(); }                \
    }

EPIX_DEFINE_TASK_POOL(ComputeTaskPool);
EPIX_DEFINE_TASK_POOL(AsyncComputeTaskPool);
EPIX_DEFINE_TASK_POOL(IoTaskPool);

#undef EPIX_DEFINE_TASK_POOL

}  // namespace epix::tasks

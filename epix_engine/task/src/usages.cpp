#include <epix/task.hpp>

namespace epix::task {

// One pool instance per pool type for the whole process, so that the engine and
// any caller resolving e.g. IoTaskPool::get() see the same pool.
#define EPIX_DEFINE_TASK_POOL_STORAGE(Name)                                        \
    Name*& Name::global_ptr() noexcept {                                           \
        static Name* ptr = nullptr;                                                \
        return ptr;                                                                \
    }                                                                              \
    Name& Name::get_or_init(TaskPool pool) {                                       \
        static std::once_flag flag;                                                \
        std::call_once(flag, [&]() { global_ptr() = new Name(std::move(pool)); }); \
        return *global_ptr();                                                      \
    }

EPIX_DEFINE_TASK_POOL_STORAGE(ComputeTaskPool)
EPIX_DEFINE_TASK_POOL_STORAGE(AsyncComputeTaskPool)
EPIX_DEFINE_TASK_POOL_STORAGE(IoTaskPool)

#undef EPIX_DEFINE_TASK_POOL_STORAGE

}  // namespace epix::task

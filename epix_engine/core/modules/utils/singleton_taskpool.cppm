module;

#define MakeTaskPool(Name)                                                \
    struct Name##TaskPool {                                               \
       private:                                                           \
        BS::thread_pool<> m_pool;                                         \
        Name##TaskPool() : m_pool(std::thread::hardware_concurrency()) {} \
                                                                          \
       public:                                                            \
        static auto& instance() {                                         \
            static Name##TaskPool pool;                                   \
            return pool.m_pool;                                           \
        }                                                                 \
    }

export module epix.utils:singleton_taskpool;

import std;
import BS.thread_pool;

namespace utils {
export MakeTaskPool(IO);
export MakeTaskPool(Worker);
}  // namespace utils
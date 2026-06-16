#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <BS_thread_pool.hpp>
#include <algorithm>
#include <thread>
#endif

namespace epix::utils {

// IOTaskPool — oversized fixed pool so blocking coordinator tasks don't
// starve each other on low-core machines (e.g. 2-core CI runners).
EPIX_EXPORT struct IOTaskPool {
   private:
    BS::thread_pool<> m_pool;
    IOTaskPool() : m_pool(std::max(std::thread::hardware_concurrency() * 2u, 8u)) {}

   public:
    static BS::thread_pool<>& instance() {
        static IOTaskPool pool;
        return pool.m_pool;
    }
};

// WorkerTaskPool — bounded pool for non-blocking CPU compute.
EPIX_EXPORT struct WorkerTaskPool {
   private:
    BS::thread_pool<> m_pool;
    WorkerTaskPool() : m_pool(std::thread::hardware_concurrency()) {}

   public:
    static BS::thread_pool<>& instance() {
        static WorkerTaskPool pool;
        return pool.m_pool;
    }
};

}  // namespace epix::utils

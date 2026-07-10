#pragma once

#ifndef EPIX_CXX_MODULE
#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/post.hpp>
#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <epix/async_task.hpp>
#include <epix/common.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#endif

namespace epix::task {
EPIX_EXPORT using async_task::Task;
EPIX_EXPORT using async_task::FallibleTask;
EPIX_EXPORT using async_task::Runnable;
EPIX_EXPORT using async_task::Waker;
EPIX_EXPORT using async_task::ScheduleInfo;
}  // namespace epix::task

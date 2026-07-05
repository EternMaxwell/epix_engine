module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#endif
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <epix/common.hpp>

export module epix.tasks;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.async_task;

extern "C++" {
#include <epix/task.hpp>
}

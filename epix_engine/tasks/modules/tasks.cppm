module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#endif
#include <asio/any_io_executor.hpp>
#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>

export module epix.tasks;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import BS.thread_pool;
extern "C++" {
#include <epix/tasks.hpp>
}

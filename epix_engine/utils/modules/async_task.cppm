module;

#ifndef EPIX_IMPORT_STD
#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#endif
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <epix/common.hpp>

export module epix.async_task;
#ifdef EPIX_IMPORT_STD
import std;
#endif

extern "C++" {
#include <epix/async_task.hpp>
}

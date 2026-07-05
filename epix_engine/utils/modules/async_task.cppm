module;

#ifndef EPIX_IMPORT_STD
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#endif
#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <epix/common.hpp>

export module epix.async_task;
#ifdef EPIX_IMPORT_STD
import std;
#endif

extern "C++" {
#include <epix/async_task.hpp>
}

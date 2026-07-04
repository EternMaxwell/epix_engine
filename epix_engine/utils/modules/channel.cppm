module;

#ifndef EPIX_IMPORT_STD
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#endif
#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <epix/common.hpp>

export module epix.async_channel;
#ifdef EPIX_IMPORT_STD
import std;
#endif

extern "C++" {
#include <epix/async_channel.hpp>
}
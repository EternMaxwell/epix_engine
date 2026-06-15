#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#endif

#include <epix/async_channel.hpp>

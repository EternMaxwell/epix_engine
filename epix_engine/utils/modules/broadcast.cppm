module;

#ifndef EPIX_IMPORT_STD
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <mutex>
#include <optional>
#endif
#include <epix/common.hpp>
#include <stdexec/execution.hpp>

export module epix.async_broadcast;
#ifdef EPIX_IMPORT_STD
import std;
#endif

extern "C++" {
#include <epix/async_broadcast.hpp>
}

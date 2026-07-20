module;

#ifndef EPIX_IMPORT_STD
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#endif
#include <epix/common.hpp>
#include <exec/start_detached.hpp>
#include <stdexec/execution.hpp>

export module epix.async_task;
#ifdef EPIX_IMPORT_STD
import std;
#endif

extern "C++" {
#include <epix/async_task.hpp>
}

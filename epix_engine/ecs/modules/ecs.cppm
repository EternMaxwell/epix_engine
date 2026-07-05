module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

#include <BS_thread_pool.hpp>

export module epix.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import epix.meta;
export import epix.traits;
export import epix.utils.core;
export import epix.task;
extern "C++" {
#include <epix/ecs.hpp>
}

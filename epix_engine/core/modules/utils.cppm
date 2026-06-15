module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <bit>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <BS_thread_pool.hpp>

export module epix.utils;
#ifdef EPIX_IMPORT_STD
import std;
#endif
extern "C++" {
#include <epix/utils.hpp>
}

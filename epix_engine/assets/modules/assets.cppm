module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>
#include <uuid.h>
#include <zpp_bits.h>

#include <stdexec/execution.hpp>
#include <efsw/efsw.hpp>

export module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.app;
import epix.ecs;
import epix.task;
import epix.async_broadcast;
import epix.async_channel;
extern "C++" {
#include <epix/assets.hpp>
}



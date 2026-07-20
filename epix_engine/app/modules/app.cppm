module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#endif

#include <spdlog/spdlog.h>

#include <epix/common.hpp>

export module epix.app;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.traits;
import epix.utils.core;
import epix.task;
import epix.ecs;
extern "C++" {
#include <epix/app.hpp>
}

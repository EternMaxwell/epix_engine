module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <epix/common.hpp>

export module epix.utils.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif

extern "C++" {
#include <epix/utils.hpp>
}
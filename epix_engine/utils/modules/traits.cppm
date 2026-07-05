module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <tuple>
#include <type_traits>
#endif
#include <epix/common.hpp>

export module epix.traits;
#ifdef EPIX_IMPORT_STD
import std;
#endif
extern "C++" {
#include <epix/traits.hpp>
}

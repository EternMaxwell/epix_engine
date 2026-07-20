module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#endif

export module epix.input;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export import epix.core;
extern "C++" {
#include <epix/input.hpp>
}

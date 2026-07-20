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
import epix.ecs;
import epix.app;
extern "C++" {
#include <epix/input.hpp>
}

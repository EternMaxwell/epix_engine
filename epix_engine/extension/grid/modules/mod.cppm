module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <stack>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <glm/glm.hpp>

export module epix.extension.grid;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.meta;
import epix.utils;
import glm;
extern "C++" {
#include <epix/extension/grid.hpp>
}

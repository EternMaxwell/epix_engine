module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif

export module epix.extension.grid_gpu;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.extension.grid;
extern "C++" {
#include <epix/extension/grid_gpu.hpp>
}

module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <spdlog/spdlog.h>
#include <zpp_bits.h>

export module epix.mesh;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.utils.core;
import epix.app;
import epix.assets;
import epix.image;
import epix.transform;
import webgpu;
import glm;
extern "C++" {
#include <epix/mesh.hpp>
}

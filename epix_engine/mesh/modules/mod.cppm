module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#endif

#include <spdlog/spdlog.h>

export module epix.mesh;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.utils.core;
import epix.app;
import epix.assets;
import epix.transform;
import webgpu;
import glm;
extern "C++" {
#include <epix/mesh.hpp>
}

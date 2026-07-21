module;
#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <zpp_bits.h>

#include <asio/awaitable.hpp>

export module epix.shader;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;
import epix.app;
import epix.assets;
import webgpu;
extern "C++" {
#include <epix/shader.hpp>
}

module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <asio/awaitable.hpp>

export module epix.image;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.app;
import epix.assets;
extern "C++" {
#include <epix/image.hpp>
}

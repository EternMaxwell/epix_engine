module;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#endif

export module epix.meta;
#ifdef EPIX_IMPORT_STD
import std;
#endif
extern "C++" {
#include <epix/meta.hpp>
}

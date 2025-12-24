#pragma once

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "fwd.hpp"

#if defined __clang__ || defined __GNUC__
#define EPIX_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define EPIX_PRETTY_FUNCTION_PREFIX '='
#define EPIX_PRETTY_FUNCTION_SUFFIX ']'
#elif defined _MSC_VER
#define EPIX_PRETTY_FUNCTION __FUNCSIG__
#define EPIX_PRETTY_FUNCTION_PREFIX '<'
#define EPIX_PRETTY_FUNCTION_SUFFIX '>'
#endif

namespace epix::core::meta {

template <typename T>
constexpr std::string_view type_name() {
    static std::string pretty_function{EPIX_PRETTY_FUNCTION};
    static auto first =
        pretty_function.find_first_not_of(' ', pretty_function.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1);
    static auto last  = pretty_function.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    static auto value = pretty_function.substr(first, last - first);
    return value;
}
static constexpr std::string shorten(std::string_view str) {
    std::string result = std::string(str);
    while (true) {
        auto last_colon = result.rfind("::");
        if (last_colon == std::string::npos) break;
        constexpr std::array left_chars = std::array{
            '<', '(', '[', ',', ' ',  // characters that can appear before a template argument
        };
        std::vector lefts = left_chars | std::views::transform([&](char c) { return result.rfind(c, last_colon); }) |
                            std::views::filter([&](size_t pos) { return pos != std::string::npos; }) |
                            std::ranges::to<std::vector>();
        auto left_elem = std::ranges::max_element(lefts);
        auto left      = (left_elem != lefts.end()) ? *left_elem + 1 : 0;
        result         = result.substr(0, left) + result.substr(last_colon + 2);
    }
    // remove all spaces
    // result.erase(std::remove_if(result.begin(), result.end(), [](char c) { return c == ' '; }), result.end());
    return result;
}
template <typename T>
constexpr std::string_view short_name() {
    static std::string name = shorten(type_name<T>());
    return name;
}
}  // namespace epix::core::meta
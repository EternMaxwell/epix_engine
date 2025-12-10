#pragma once

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "../../api/macros.hpp"
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

EPIX_MODULE_EXPORT namespace epix::core::meta {

/**
 * @brief Get the full type name of T at compile time.
 */
template <typename T>
constexpr std::string_view type_name() {
    static std::string pretty_function{EPIX_PRETTY_FUNCTION};
    static auto first =
        pretty_function.find_first_not_of(' ', pretty_function.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1);
    static auto last  = pretty_function.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    static auto value = pretty_function.substr(first, last - first);
    return value;
}

/**
 * @brief Shorten a fully qualified type name by removing namespace prefixes.
 */
static constexpr std::string shorten(std::string_view str) {
    std::string result = std::string(str);
    while (true) {
        auto last_colon = result.rfind("::");
        if (last_colon == std::string::npos) break;
        constexpr std::array left_chars = std::array{
            '<', '(', '[', ',', ' ',  // characters that can appear before a template argument
        };
#if EPIX_HAS_RANGES_TO
        std::vector lefts = left_chars | std::views::transform([&](char c) { return result.rfind(c, last_colon); }) |
                            std::views::filter([&](size_t pos) { return pos != std::string::npos; }) |
                            std::ranges::to<std::vector>();
#else
        std::vector<size_t> lefts;
        for (auto c : left_chars) {
            auto pos = result.rfind(c, last_colon);
            if (pos != std::string::npos) {
                lefts.push_back(pos);
            }
        }
#endif
        auto left_elem = std::ranges::max_element(lefts);
        auto left      = (left_elem != lefts.end()) ? *left_elem + 1 : size_t{0};
        result         = result.substr(0, left) + result.substr(last_colon + 2);
    }
    return result;
}

/**
 * @brief Get a shortened type name of T at compile time.
 */
template <typename T>
constexpr std::string_view short_name() {
    static std::string name = shorten(type_name<T>());
    return name;
}

/**
 * @brief Type identity and metadata accessor.
 */
template <typename T>
struct type_id {
   public:
    static std::string_view name() {
        static std::string_view value = type_name<T>();
        return value;
    }
    static std::string_view short_name() {
        static std::string_view value = meta::short_name<T>();
        return value;
    }
    static size_t hash_code() {
        static size_t hash = std::hash<std::string_view>()(name());
        return hash;
    }
};
}  // namespace epix::core::meta
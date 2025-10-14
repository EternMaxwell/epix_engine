#pragma once

#include <string>
#include <string_view>

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
        auto left = std::min(result.rfind('<', last_colon), result.rfind(',', last_colon));
        left      = left == std::string::npos ? 0 : left + 1;
        result    = result.substr(0, left) + result.substr(last_colon + 2);
    }
    // remove all spaces
    result.erase(std::remove_if(result.begin(), result.end(), [](char c) { return c == ' '; }), result.end());
    return result;
}
template <typename T>
constexpr std::string_view short_name() {
    static std::string name = shorten(type_name<T>());
    return name;
}

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
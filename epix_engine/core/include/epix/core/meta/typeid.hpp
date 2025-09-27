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
constexpr const char* type_name() {
    static std::string pretty_function{EPIX_PRETTY_FUNCTION};
    static auto first =
        pretty_function.find_first_not_of(' ', pretty_function.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1);
    static auto last  = pretty_function.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    static auto value = pretty_function.substr(first, last - first);
    return value.c_str();
}

template <typename T>
struct type_id {
   public:
    static std::string_view name() {
        static std::string_view value = type_name<T>();
        return value;
    }
    static size_t hash_code() {
        static size_t hash = std::hash<std::string_view>()(name());
        return hash;
    }
};
}  // namespace epix::core::meta
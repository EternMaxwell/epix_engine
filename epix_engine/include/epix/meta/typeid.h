#pragma once

#include <string>
#include <string_view>

#include "epix/common.h"

#if defined __clang__ || defined __GNUC__
#define EPIX_PRETTY_FUNCTION __PRETTY_FUNCTION__
#define EPIX_PRETTY_FUNCTION_PREFIX '='
#define EPIX_PRETTY_FUNCTION_SUFFIX ']'
#elif defined _MSC_VER
#define EPIX_PRETTY_FUNCTION __FUNCSIG__
#define EPIX_PRETTY_FUNCTION_PREFIX '<'
#define EPIX_PRETTY_FUNCTION_SUFFIX '>'
#endif

namespace epix::meta {
template <typename T>
constexpr std::string type_name() {
    std::string pretty_function{EPIX_PRETTY_FUNCTION};
    auto first = pretty_function.find_first_not_of(
        ' ', pretty_function.find_first_of(EPIX_PRETTY_FUNCTION_PREFIX) + 1
    );
    auto last  = pretty_function.find_last_of(EPIX_PRETTY_FUNCTION_SUFFIX);
    auto value = pretty_function.substr(first, last - first);
    return value;
}

template <typename T>
struct type_id {
    inline static const std::string name = type_name<T>();
};

struct type_index {
   private:
    std::string_view value;

    friend struct std::hash<type_index>;

   public:
    template <typename T>
    type_index(type_id<T>) : value(type_id<T>::name) {}
    inline bool operator==(const type_index& other) const noexcept {
        return value == other.value;
    }
    inline size_t hash_code() const noexcept {
        return std::hash<std::string_view>()(value);
    }
    inline std::string_view name() const noexcept { return value; }
};
}  // namespace epix::meta

template <>
struct std::hash<epix::meta::type_index> {
    inline size_t operator()(const epix::meta::type_index& index
    ) const noexcept {
        return index.hash_code();
    }
};
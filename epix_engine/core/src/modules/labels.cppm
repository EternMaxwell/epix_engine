module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


export module epix.core:labels;

import :label;

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::core::Label {                                              \
       public:                                                                        \
        type() = default;                                                             \
        template <typename T>                                                         \
        type(T t)                                                                     \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

namespace core {
export EPIX_MAKE_LABEL(SystemSetLabel);
export EPIX_MAKE_LABEL(ScheduleLabel);
export EPIX_MAKE_LABEL(AppLabel);
}  // namespace core

// Explicit std::hash specialization for AppLabel to avoid MSVC instantiation bug
// namespace std {
// template <>
// struct hash<::core::AppLabel> {
//     size_t operator()(const ::core::AppLabel& label) const noexcept { return std::hash<::core::Label>()(label); }
// };
// }  // namespace std
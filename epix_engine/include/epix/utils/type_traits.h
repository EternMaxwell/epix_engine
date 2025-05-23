#pragma once

#include <type_traits>

namespace epix::util::type_traits {
template <typename T, template <typename...> typename U>
struct is_specialization_of : std::false_type {};
template <template <typename...> typename U, typename... Args>
struct is_specialization_of<U<Args...>, U> : std::true_type {};
}  // namespace epix::util::type_traits

namespace epix::util::type_traits {
template <typename T, template <typename...> typename U>
concept specialization_of = is_specialization_of<T, U>::value;
}
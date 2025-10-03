#pragma once

#include <cstddef>
#include <ranges>
#include <tuple>
#include <type_traits>

#include "fwd.hpp"

namespace epix::core {
namespace bundle {
template <template <typename...> typename Templated, typename T>
struct is_specialization_of : std::false_type {};
template <template <typename...> typename Templated, typename... Ts>
struct is_specialization_of<Templated, Templated<Ts...>> : std::true_type {};
template <template <typename...> typename Templated, typename T>
concept specialization_of = is_specialization_of<Templated, T>::value;
template <typename V, typename T>
struct is_constructible_from_tuple : std::false_type {};
template <typename T, typename... Args>
struct is_constructible_from_tuple<T, std::tuple<Args...>> : std::bool_constant<std::is_constructible_v<T, Args...>> {};
template <typename V, typename T>
concept constructible_from_tuple = is_constructible_from_tuple<V, T>::value;

template <typename R>
concept is_void_ptr_view = requires(R r) {
    { std::ranges::view<R> };
    { std::ranges::sized_range<R> };
    { std::same_as<std::ranges::range_value_t<R>, void*> };
};
}  // namespace bundle
/**
 * @brief Detail of a bundle type.
 * The first template parameter is a tuple of the types that will explicitly added to the entity by this bundle.
 * The second template parameter is a tuple of tuples, each inner tuple contains the argument types for constructing
 * the corresponding type in the first tuple.
 *
 * @tparam Ts
 * @tparam Args
 */
template <typename Ts, typename Args>
struct BundleDetail {
    static_assert(false, "BundleDetail must be specialized for std::tuple types");
};
template <typename... Ts, typename... ArgTuples>
    requires((bundle::specialization_of<std::tuple, ArgTuples> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (bundle::constructible_from_tuple<Ts, ArgTuples> && ...))
struct BundleDetail<std::tuple<Ts...>, std::tuple<ArgTuples...>> {
    // stores the args for constructing each component in Ts
    using storage_type = std::tuple<ArgTuples...>;
    storage_type args;

    void initialize_at(bundle::is_void_ptr_view auto&& pointers) {
        assert(std::ranges::size(pointers) == sizeof...(Ts));
        auto&& ptr_it = std::ranges::begin(pointers);

        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<size_t I>(std::integral_constant<size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    T* ptr       = static_cast<T*>(*ptr_it);
                    []<size_t... Js>(std::index_sequence<Js...>, T* p, ATuple& atuple) {
                        new (p) T(std::forward<std::tuple_element_t<Js, ATuple>>(
                            std::get<Js>(atuple))...);  // construct T in place with args from atuple
                    }(std::make_index_sequence<std::tuple_size_v<ATuple>>{}, ptr, std::get<I>(args));
                    ++ptr_it;
                }(std::integral_constant<size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }
};
template <typename... Ts, typename... ArgTuples>
    requires((bundle::specialization_of<std::tuple, ArgTuples> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (bundle::constructible_from_tuple<Ts, ArgTuples> && ...))
BundleDetail<std::tuple<Ts...>, std::tuple<ArgTuples...>> make_bundle(ArgTuples&&... args) {
    return BundleDetail<std::tuple<Ts...>, std::tuple<ArgTuples...>>{std::make_tuple(std::forward<ArgTuples>(args)...)};
}
struct BundleInfo {};
}  // namespace epix::core
/**
 * @file epix.core-bundle.cppm
 * @brief Bundle partition for component bundles
 */

export module epix.core:bundle;

import :fwd;
import :type_system;
import :entities;
import :component;

#include <concepts>
#include <cstddef>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

export namespace epix::core::bundle {
    template <typename T, template <typename...> typename Templated>
    struct is_specialization_of : std::false_type {};
    
    template <template <typename...> typename Templated, typename... Ts>
    struct is_specialization_of<Templated<Ts...>, Templated> : std::true_type {};
    
    template <typename T, template <typename...> typename Templated>
    concept specialization_of = is_specialization_of<T, Templated>::value;
    
    template <typename V, typename T>
    struct is_constructible_from_tuple : std::false_type {};
    
    template <typename T, typename... Args>
    struct is_constructible_from_tuple<T, std::tuple<Args...>> : std::bool_constant<std::is_constructible_v<T, Args...>> {};
    
    template <typename V, typename T>
    concept constructible_from_tuple = is_constructible_from_tuple<V, T>::value;

    template <typename T>
    struct Bundle {};
    
    template <typename R>
    concept is_void_ptr_view = requires(R r) {
        { std::ranges::view<R> };
        { std::ranges::sized_range<R> };
        { std::same_as<std::ranges::range_value_t<R>, void*> };
    };

    template <typename R>
    concept type_id_view = requires(R r) {
        { std::ranges::view<R> };
        { std::ranges::sized_range<R> };
        { std::same_as<std::ranges::range_value_t<R>, type_system::TypeId> };
    };
    
    template <typename T>
    concept is_bundle = requires { typename Bundle<T>; };
}  // namespace epix::core::bundle

export namespace epix::core {
    struct Bundles {
        // Bundle registry implementation
    };
}  // namespace epix::core

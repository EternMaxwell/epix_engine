#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <epix/traits.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/ecs/bundle/info.hpp>

namespace epix::ecs {
namespace internal {
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
struct InitializeBundle {
    static_assert(false, "BundleDetail must be specialized for std::tuple types");
};
template <typename... Ts, typename... ArgTuples>
    requires((traits::specialization_of<ArgTuples, std::tuple> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             ((traits::constructible_from_tuple<Ts, ArgTuples>/*  ||
               (std::same_as<Ts, std::monostate> && (std::tuple_size_v<ArgTuples> == 1) &&
                is_bundle<std::tuple_element_t<0, ArgTuples>>) */) &&
              ...))
struct InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> {
    // stores the args for constructing each component in Ts
    using storage_type = std::tuple<ArgTuples...>;
    storage_type args;

    /**
     * @brief Construct bundle types in place at the provided pointers.
     * The stored argument values should have lifetimes that extend beyond this call.
     * @param pointers
     */
    void get_components(utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) noexcept {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    // if ATuple has only one element and that element is same as T after decay, and element is
                    // bundle, then write as a bundle
                    if constexpr (std::tuple_size_v<ATuple> == 1 &&
                                  (std::same_as<T, std::decay_t<std::tuple_element_t<0, ATuple>>> ||
                                   std::same_as<T, std::monostate>) &&
                                  is_bundle<std::tuple_element_t<0, ATuple>>) {
                        // write as a bundle
                        using BundleType = Bundle<std::decay_t<std::tuple_element_t<0, ATuple>>>;
                        BundleType::get_components(std::get<0>(std::get<I>(args)), write_component);
                    } else {
                        // write as a single component
                        write_component([&](void* ptr) {
                            std::apply(
                                [ptr](auto&&... unpacked_args) {
                                    new (ptr) T(std::forward<decltype(unpacked_args)>(unpacked_args)...);
                                },
                                std::forward<ATuple>(std::get<I>(args)));
                        });
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }

    static std::vector<std::optional<TypeId>> type_ids(const Components& components) {
        std::vector<std::optional<TypeId>> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    if constexpr (std::tuple_size_v<ATuple> == 1 &&
                                  std::same_as<T, std::decay_t<std::tuple_element_t<0, ATuple>>> &&
                                  is_bundle<std::tuple_element_t<0, ATuple>>) {
                        // bundle type
                        using BundleType = Bundle<std::decay_t<std::tuple_element_t<0, ATuple>>>;
                        ids.append_range(BundleType::type_ids(components));
                    } else {
                        ids.push_back(components.get_id<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return std::move(ids);
    }
    static std::vector<TypeId> register_components(ComponentsRegistrator& components) {
        std::vector<TypeId> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    if constexpr (std::tuple_size_v<ATuple> == 1 &&
                                  std::same_as<T, std::decay_t<std::tuple_element_t<0, ATuple>>> &&
                                  is_bundle<std::tuple_element_t<0, ATuple>>) {
                        // bundle type
                        using BundleType = Bundle<std::decay_t<std::tuple_element_t<0, ATuple>>>;
                        ids.append_range(BundleType::register_components(components));
                    } else {
                        ids.push_back(components.register_component<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return ids;
    }
};

template <typename... Ts>
struct RemoveBundle {
    void get_components(utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) const noexcept {}
    static std::vector<std::optional<TypeId>> type_ids(const Components& components) {
        std::vector<std::optional<TypeId>> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
                    if constexpr (is_bundle<T>) {
                        using BundleType = Bundle<T>;
                        ids.append_range(BundleType::type_ids(components));
                    } else {
                        ids.push_back(components.get_id<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return std::move(ids);
    }
    static std::vector<TypeId> register_components(ComponentsRegistrator& components) {
        std::vector<TypeId> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
                    if constexpr (is_bundle<T>) {
                        using BundleType = Bundle<T>;
                        ids.append_range(BundleType::register_components(components));
                    } else {
                        ids.push_back(components.register_component<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return ids;
    }
};
}  // namespace internal
template <typename T>
    requires(traits::specialization_of<T, internal::InitializeBundle>)
struct Bundle<T> {
    static void get_components(T& bundle,
                               std::invocable<utils::function_ref<void(void*)>> auto&& write_component) noexcept {
        bundle.get_components(write_component);
    }
    static auto type_ids(const Components& components) { return T::type_ids(components); }
    static auto register_components(ComponentsRegistrator& components) { return T::register_components(components); }
};
template <typename T>
    requires(traits::specialization_of<T, internal::RemoveBundle>)
struct Bundle<T> {
    static void get_components(T& bundle,
                               std::invocable<utils::function_ref<void(void*)>> auto&& write_component) noexcept {
        bundle.get_components(write_component);
    }
    static auto type_ids(const Components& components) { return T::type_ids(components); }
    static auto register_components(ComponentsRegistrator& components) { return T::register_components(components); }
};
EPIX_EXPORT template <typename... Ts, typename... ArgTuples>
    requires((traits::specialization_of<ArgTuples, std::tuple> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (traits::constructible_from_tuple<Ts, ArgTuples> && ...)) &&
            (sizeof...(ArgTuples) > 0)
internal::InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> make_bundle(ArgTuples&&... args) {
    return internal::InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>>{
        std::make_tuple(std::forward<ArgTuples>(args)...)};
}
EPIX_EXPORT template <typename... Ts>
    requires(std::constructible_from<std::decay_t<Ts>, Ts> && ...)
internal::InitializeBundle<std::tuple<std::decay_t<Ts>...>, std::tuple<std::tuple<Ts>...>> make_bundle(Ts&&... args) {
    return internal::InitializeBundle<std::tuple<std::decay_t<Ts>...>, std::tuple<std::tuple<Ts>...>>{
        std::make_tuple(std::tuple<Ts>(std::forward<Ts>(args))...)};
}
}  // namespace epix::ecs
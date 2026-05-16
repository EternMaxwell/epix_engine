module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
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
#include <cassert>

export module epix.core:bundle.built_in;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :bundle.info;

namespace epix::core {
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
    requires((specialization_of<ArgTuples, std::tuple> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             ((constructible_from_tuple<Ts, ArgTuples>/*  ||
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
                    // if ATuple has only one element and that element is same as T after decay, and element is bundle,
                    // then write as a bundle
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

    static auto type_ids(const TypeRegistry& registry) {
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
                        ids.insert_range(ids.end(), BundleType::type_ids(registry));
                    } else {
                        ids.push_back(registry.type_id<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return std::move(ids);
    }
    static void register_components(const TypeRegistry& registry, Components& components) {
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
                        BundleType::register_components(registry, components);
                    } else {
                        components.register_info<T>();
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }
};
export template <typename... Ts, typename... ArgTuples>
    requires((specialization_of<ArgTuples, std::tuple> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (constructible_from_tuple<Ts, ArgTuples> && ...)) &&
            (sizeof...(ArgTuples) > 0)
InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> make_bundle(ArgTuples&&... args) {
    return InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>>{
        std::make_tuple(std::forward<ArgTuples>(args)...)};
}
export template <typename... Ts>
    requires(std::constructible_from<std::decay_t<Ts>, Ts> && ...)
InitializeBundle<std::tuple<std::decay_t<Ts>...>, std::tuple<std::tuple<Ts>...>> make_bundle(Ts&&... args) {
    return InitializeBundle<std::tuple<std::decay_t<Ts>...>, std::tuple<std::tuple<Ts>...>>{
        std::make_tuple(std::tuple<Ts>(std::forward<Ts>(args))...)};
}
template <typename T>
    requires(specialization_of<T, InitializeBundle>)
struct Bundle<T> {
    static void get_components(T& bundle,
                               utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) noexcept {
        bundle.get_components(write_component);
    }
    static auto type_ids(const TypeRegistry& registry) { return T::type_ids(registry); }
    static void register_components(const TypeRegistry& registry, Components& components) {
        T::register_components(registry, components);
    }
};

template <typename... Ts>
struct RemoveBundle {
    void get_components(utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) const noexcept {}
    static auto type_ids(const TypeRegistry& registry) {
        std::vector<TypeId> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
                    if constexpr (is_bundle<T>) {
                        using BundleType = Bundle<T>;
                        ids.insert_range(ids.end(), BundleType::type_ids(registry));
                    } else {
                        ids.push_back(registry.type_id<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return std::move(ids);
    }
    static void register_components(const TypeRegistry& registry, Components& components) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
                    if constexpr (is_bundle<T>) {
                        using BundleType = Bundle<T>;
                        BundleType::register_components(registry, components);
                    } else {
                        components.register_info<T>();
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }
};
template <typename T>
    requires(specialization_of<T, RemoveBundle>)
struct Bundle<T> {
    static void get_components(T& bundle,
                               utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) noexcept {
        bundle.get_components(write_component);
    }
    static auto type_ids(const TypeRegistry& registry) { return T::type_ids(registry); }
    static void register_components(const TypeRegistry& registry, Components& components) {
        T::register_components(registry, components);
    }
};
}  // namespace epix::core
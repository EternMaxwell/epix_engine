#pragma once

#include <concepts>
#include <optional>
#include <type_traits>

#include "../fwd.hpp"

namespace epix::core {
template <typename T>
concept is_construct_from_world = std::constructible_from<T, World&>;
template <typename T>
concept is_static_from_world = requires(World& world) {
    { T::from_world(world) } -> std::same_as<T>;
};
template <typename T>
concept is_nothrow_static_from_world = requires(World& world) {
    { T::from_world(world) } noexcept -> std::same_as<T>;
};
// template <typename T>
// concept is_optional_from_world = requires(World& world) {
//     { T::from_world(world) } -> std::same_as<std::optional<T>>;
// };

template <typename T>
concept is_from_world = (std::is_default_constructible<T>::value) || is_construct_from_world<T> ||
                        is_static_from_world<T> /*  || is_optional_from_world<T> */;

template <is_from_world T>
struct FromWorld {
    static inline constexpr bool default_construct    = std::is_default_constructible<T>::value;
    static inline constexpr bool construct_from_world = is_construct_from_world<T>;
    static inline constexpr bool static_from_world    = is_static_from_world<T>;
    static inline constexpr bool is_noexcept =
        (default_construct && std::is_nothrow_default_constructible<T>::value) ||
        (construct_from_world && std::is_nothrow_constructible<T, World&>::value) ||
        (static_from_world && is_nothrow_static_from_world<T>);

    static T create(World& world) noexcept(is_noexcept) {
        if constexpr (is_construct_from_world<T>) {
            return T(world);
        } else if constexpr (is_static_from_world<T>) {
            return T::from_world(world);
            // } else if constexpr (is_optional_from_world<T>) {
            //     return T::from_world(world).value();  // will throw if std::nullopt
        } else if constexpr (std::is_default_constructible<T>::value) {
            return T{};
        } else {
            static_assert(false, "Unreachable");
        }
    }
    static std::optional<T> try_create(World& world) noexcept {
        try {
            if constexpr (is_construct_from_world<T>) {
                return T(world);
            } else if constexpr (is_static_from_world<T>) {
                return T::from_world(world);
                // } else if constexpr (is_optional_from_world<T>) {
                //     return T::from_world(world);
            } else if constexpr (std::is_default_constructible<T>::value) {
                return T{};
            } else {
                static_assert(false, "Unreachable");
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    static T* create_ptr(World& world) noexcept(is_noexcept) {
        if constexpr (is_construct_from_world<T>) {
            return new T(world);
        } else if constexpr (is_static_from_world<T>) {
            return new T(T::from_world(world));
            // } else if constexpr (is_optional_from_world<T>) {
            //     std::optional<T> val = T::from_world(world);
            //     if (val.has_value()) {
            //         return new T(std::move(*val));
            //     } else {
            //         return nullptr;
            //     }
        } else if constexpr (std::is_default_constructible<T>::value) {
            return new T();
        } else {
            static_assert(false, "Unreachable");
        }
    }
    static void emplace(void* dest, World& world) noexcept(is_noexcept) {
        if constexpr (is_construct_from_world<T>) {
            new (dest) T(world);
        } else if constexpr (is_static_from_world<T>) {
            new (dest) T(T::from_world(world));
            // } else if constexpr (is_optional_from_world<T>) {
            //     std::optional<T> val = T::from_world(world);
            //     if (val.has_value()) {
            //         new (dest) T(std::move(*val));
            //         return true;
            //     } else {
            //         return false;
            //     }
        } else if constexpr (std::is_default_constructible<T>::value) {
            new (dest) T();
        } else {
            static_assert(false, "Unreachable");
        }
    }
};
}  // namespace epix::core
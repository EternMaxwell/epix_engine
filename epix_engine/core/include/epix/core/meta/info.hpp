#pragma once

#include <cstdlib>
#include <string_view>
#include <type_traits>

#include "fwd.hpp"
#include "name.hpp"

namespace epix::core::meta {
struct type_info {
    using destruct_fn       = void (*)(void* ptr) noexcept;
    using copy_construct_fn = void (*)(void* dest, const void* src) noexcept;
    using move_construct_fn = void (*)(void* dest, void* src) noexcept;

    std::string_view name;
    std::string_view short_name;
    size_t hash;

    size_t size;
    size_t align;

    // Mandatory operations
    void (*destruct)(void* ptr) noexcept                         = nullptr;
    void (*copy_construct)(void* dest, const void* src) noexcept = nullptr;
    void (*move_construct)(void* dest, void* src) noexcept       = nullptr;

    // Cached traits
    bool trivially_copyable;
    bool trivially_destructible;
    bool noexcept_move_constructible;
    bool noexcept_copy_constructible;

    template <typename T>
    static destruct_fn destruct_impl() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            return [](void* p) noexcept { static_cast<T*>(p)->~T(); };
        } else if constexpr (std::is_destructible_v<T>) {
            return [](void* p) noexcept {
                // no-op for trivially destructible types
            };
        } else {
            return [](void* p) noexcept { std::abort(); };
        }
    }
    template <typename T>
    static copy_construct_fn copy_construct_impl() {
        if constexpr (std::copy_constructible<T>) {
            return [](void* dest, const void* src) noexcept { new (dest) T(*static_cast<const T*>(src)); };
        } else {
            return [](void* dest, const void* src) noexcept { std::abort(); };
        }
    }
    template <typename T>
    static move_construct_fn move_construct_impl() {
        if constexpr (std::move_constructible<T>) {
            return [](void* dest, void* src) noexcept { new (dest) T(std::move(*static_cast<T*>(src))); };
        } else {
            return [](void* dest, void* src) noexcept { std::abort(); };
        }
    }

    template <typename T>
    static const type_info* of() {
        if constexpr (requires { sizeof(T); }) {
            return of1<T>();
        } else {
            return of2<T>();
        }
    }

   private:
    template <typename T>
    static const type_info* of1() {
        static type_info ti = type_info{
            .name                        = meta::type_name<T>(),
            .short_name                  = meta::short_name<T>(),
            .hash                        = std::hash<std::string_view>()(meta::type_name<T>()),
            .size                        = sizeof(T),
            .align                       = alignof(T),
            .destruct                    = destruct_impl<T>(),
            .copy_construct              = copy_construct_impl<T>(),
            .move_construct              = move_construct_impl<T>(),
            .trivially_copyable          = std::is_trivially_copyable_v<T>,
            .trivially_destructible      = std::is_trivially_destructible_v<T>,
            .noexcept_move_constructible = std::is_nothrow_move_constructible_v<T>,
            .noexcept_copy_constructible = std::is_nothrow_copy_constructible_v<T>,
        };
        return &ti;
    }
    template <typename T>
    static const type_info* of2() {
        static type_info ti = type_info{
            .name                        = meta::type_name<T>(),
            .short_name                  = meta::short_name<T>(),
            .hash                        = std::hash<std::string_view>()(meta::type_name<T>()),
            .size                        = 0,
            .align                       = 0,
            .destruct                    = nullptr,
            .copy_construct              = nullptr,
            .move_construct              = nullptr,
            .trivially_copyable          = false,
            .trivially_destructible      = false,
            .noexcept_move_constructible = false,
            .noexcept_copy_constructible = false,
        };
        return &ti;
    }
};
}  // namespace epix::core::meta
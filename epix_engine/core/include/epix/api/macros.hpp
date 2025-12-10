#pragma once

// Include module compatibility header for feature test macros
#include "module_compat.hpp"

#if defined(_WIN32)
#define EPIX_EXPORT_SYMBOL __declspec(dllexport)
#define EPIX_IMPORT_SYMBOL __declspec(dllimport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define EPIX_EXPORT_SYMBOL __attribute__((visibility("default")))
#define EPIX_IMPORT_SYMBOL __attribute__((visibility("default")))
#else
#define EPIX_EXPORT_SYMBOL
#define EPIX_IMPORT_SYMBOL
#endif

// Legacy macros for backward compatibility
#ifndef EPIX_EXPORT
#define EPIX_EXPORT EPIX_EXPORT_SYMBOL
#endif
#ifndef EPIX_IMPORT
#define EPIX_IMPORT EPIX_IMPORT_SYMBOL
#endif

#if defined(EPIX_BUILD_SHARED)
#define EPIX_API EPIX_EXPORT_SYMBOL
#elif defined(EPIX_DLL) || defined(EPIX_SHARED)
#define EPIX_API EPIX_IMPORT_SYMBOL
#else
#define EPIX_API
#endif

#include <concepts>
#include <cstdint>
#include <functional>
#include <utility>

EPIX_MODULE_EXPORT namespace epix::core::wrapper {
/**
 * @brief Base class for strongly-typed integer wrappers.
 *
 * Provides a type-safe wrapper around integral types to prevent
 * implicit conversions between semantically different integer values.
 *
 * @tparam T The underlying integral type.
 */
template <typename T>
    requires std::is_integral_v<T>
struct int_base {
   public:
    using value_type = T;

    constexpr int_base(T v = 0) : value(v) {}
    
#if EPIX_HAS_DEDUCING_THIS
    constexpr T get(this int_base self) { return self.value; }
    constexpr void set(this int_base& self, T v) { self.value = v; }
#else
    constexpr T get() const { return value; }
    constexpr void set(T v) { value = v; }
#endif

    constexpr auto operator<=>(const int_base&) const = default;
    constexpr operator T() const { return value; }
    constexpr operator size_t()
        requires(!std::same_as<T, size_t>)
    {
        return static_cast<size_t>(value);
    }

   protected:
    T value;
};
}  // namespace epix::core::wrapper

namespace std {
template <typename T>
    requires std::is_integral_v<T>
struct hash<epix::core::wrapper::int_base<T>> {
    size_t operator()(const epix::core::wrapper::int_base<T>& v) const { return std::hash<T>()(v.get()); }
};
template <typename T>
    requires std::derived_from<T, epix::core::wrapper::int_base<typename T::value_type>>
struct hash<T> {
    size_t operator()(const T& v) const {
        return std::hash<epix::core::wrapper::int_base<typename T::value_type>>()(v);
    }
};
}  // namespace std

#ifndef EPIX_MAKE_INT_WRAPPER
#define EPIX_MAKE_INT_WRAPPER(type, int_type)                        \
    struct type : public ::epix::core::wrapper::int_base<int_type> { \
       public:                                                       \
        using ::epix::core::wrapper::int_base<int_type>::int_base;   \
    };
#endif

#ifndef EPIX_MAKE_U32_WRAPPER
#define EPIX_MAKE_U32_WRAPPER(type) EPIX_MAKE_INT_WRAPPER(type, uint32_t)
#endif

#ifndef EPIX_MAKE_U64_WRAPPER
#define EPIX_MAKE_U64_WRAPPER(type) EPIX_MAKE_INT_WRAPPER(type, uint64_t)
#endif
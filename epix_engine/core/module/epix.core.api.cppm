/**
 * @file epix.core.api.cppm
 * @brief C++20 module interface for epix core API definitions.
 *
 * This module provides API export/import macros and common wrapper types
 * used throughout the engine.
 */
module;

#include <concepts>
#include <cstdint>
#include <functional>
#include <utility>

export module epix.core.api;

// Platform-specific export/import macros
#if defined(_WIN32)
#define EPIX_EXPORT __declspec(dllexport)
#define EPIX_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define EPIX_EXPORT __attribute__((visibility("default")))
#define EPIX_IMPORT __attribute__((visibility("default")))
#else
#define EPIX_EXPORT
#define EPIX_IMPORT
#endif

#if defined(EPIX_BUILD_SHARED)
#define EPIX_API EPIX_EXPORT
#elif defined(EPIX_DLL) || defined(EPIX_SHARED)
#define EPIX_API EPIX_IMPORT
#else
#define EPIX_API
#endif

export namespace epix::core::wrapper {

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
    constexpr T get(this int_base self) { return self.value; }
    constexpr void set(this int_base& self, T v) { self.value = v; }
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

// Hash specializations for int_base and derived types
export template <typename T>
    requires std::is_integral_v<T>
struct std::hash<epix::core::wrapper::int_base<T>> {
    size_t operator()(const epix::core::wrapper::int_base<T>& v) const { return std::hash<T>()(v.get()); }
};

export template <typename T>
    requires std::derived_from<T, epix::core::wrapper::int_base<typename T::value_type>>
struct std::hash<T> {
    size_t operator()(const T& v) const {
        return std::hash<epix::core::wrapper::int_base<typename T::value_type>>()(v);
    }
};

// Macro definitions for creating wrapper types
// Note: In C++20 modules, macros are not exported, so users need to include
// a header with these macros or define them in their own code.

// Export convenience type aliases
export namespace epix::core {

using wrapper::int_base;

}  // namespace epix::core

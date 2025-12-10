#pragma once

/**
 * @file features.hpp
 * @brief Feature test macros for C++20/C++23 compatibility
 * 
 * This file provides compatibility macros for features that may not be
 * available across all supported compilers (GCC 13, Clang 18).
 */

// Check for C++20 modules support
#if defined(__cpp_modules) && __cpp_modules >= 201907L
    #define EPIX_HAS_MODULES 1
#else
    #define EPIX_HAS_MODULES 0
#endif

// Check for C++23 std::expected
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
    #define EPIX_HAS_EXPECTED 1
    #include <expected>
#else
    #define EPIX_HAS_EXPECTED 0
    // Fallback or error for older compilers
    #error "std::expected is required but not available"
#endif

// Check for C++23 std::move_only_function
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
    #define EPIX_HAS_MOVE_ONLY_FUNCTION 1
    #include <functional>
#else
    #define EPIX_HAS_MOVE_ONLY_FUNCTION 0
    // Fallback to std::function for older compilers
    #include <functional>
    namespace epix::compat {
        template<typename Sig>
        using move_only_function = std::function<Sig>;
    }
    // Note: User code should use epix::compat::move_only_function when fallback is needed
#endif

// Check for C++20 concepts
#if defined(__cpp_concepts) && __cpp_concepts >= 202002L
    #define EPIX_HAS_CONCEPTS 1
#else
    #define EPIX_HAS_CONCEPTS 0
    #error "C++20 concepts are required"
#endif

// Check for C++20 ranges
#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 202110L
    #define EPIX_HAS_RANGES 1
#else
    #define EPIX_HAS_RANGES 0
    #error "C++20 ranges are required"
#endif

// Check for C++23 std::format
#if defined(__cpp_lib_format) && __cpp_lib_format >= 202207L
    #define EPIX_HAS_FORMAT 1
    #include <format>
#elif defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
    #define EPIX_HAS_FORMAT 1
    #include <format>
    // Note: Some features might be limited in C++20 std::format
#else
    #define EPIX_HAS_FORMAT 0
    // Could fall back to spdlog::fmt if needed
    #error "std::format is required but not available"
#endif

// Check for C++20 span
#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
    #define EPIX_HAS_SPAN 1
#else
    #define EPIX_HAS_SPAN 0
    #error "std::span is required"
#endif

// Check for C++23 std::mdspan
#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
    #define EPIX_HAS_MDSPAN 1
    #include <mdspan>
#else
    #define EPIX_HAS_MDSPAN 0
    // mdspan is optional, can be implemented separately if needed
#endif

// GCC vs Clang specific workarounds
#if defined(__GNUC__) && !defined(__clang__)
    #define EPIX_COMPILER_GCC 1
    #define EPIX_COMPILER_CLANG 0
#elif defined(__clang__)
    #define EPIX_COMPILER_GCC 0
    #define EPIX_COMPILER_CLANG 1
#else
    #define EPIX_COMPILER_GCC 0
    #define EPIX_COMPILER_CLANG 0
#endif

// Module export macro
#if EPIX_HAS_MODULES
    #define EPIX_MODULE_EXPORT export
#else
    #define EPIX_MODULE_EXPORT
#endif

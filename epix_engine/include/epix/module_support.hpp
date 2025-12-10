#pragma once

/**
 * @file module_support.hpp
 * @brief C++20 modules and C++23 feature detection support
 * 
 * This file provides macros and utilities for conditional compilation
 * based on C++20 module support and C++23 features.
 */

// C++23 feature detection
#if __cplusplus >= 202302L
    #define EPIX_HAS_CPP23 1
#else
    #define EPIX_HAS_CPP23 0
#endif

// C++20 feature detection
#if __cplusplus >= 202002L
    #define EPIX_HAS_CPP20 1
#else
    #define EPIX_HAS_CPP20 0
#endif

// Compiler-specific module support detection
#if defined(__has_include)
    #if __has_include(<version>)
        #include <version>
    #endif
#endif

// Check for C++23 std::expected
#ifdef __cpp_lib_expected
    #define EPIX_HAS_STD_EXPECTED 1
#else
    #define EPIX_HAS_STD_EXPECTED 0
#endif

// Check for C++23 std::mdspan
#ifdef __cpp_lib_mdspan
    #define EPIX_HAS_STD_MDSPAN 1
#else
    #define EPIX_HAS_STD_MDSPAN 0
#endif

// Check for C++23 std::print
#ifdef __cpp_lib_print
    #define EPIX_HAS_STD_PRINT 1
#else
    #define EPIX_HAS_STD_PRINT 0
#endif

// Check for C++23 std::flat_map
#ifdef __cpp_lib_flat_map
    #define EPIX_HAS_STD_FLAT_MAP 1
#else
    #define EPIX_HAS_STD_FLAT_MAP 0
#endif

// Check for C++23 std::generator
#ifdef __cpp_lib_generator
    #define EPIX_HAS_STD_GENERATOR 1
#else
    #define EPIX_HAS_STD_GENERATOR 0
#endif

// Check for C++23 deducing this
#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
    #define EPIX_HAS_DEDUCING_THIS 1
#else
    #define EPIX_HAS_DEDUCING_THIS 0
#endif

// Check for C++20 modules
#if defined(__cpp_modules) && __cpp_modules >= 201907L
    #define EPIX_HAS_MODULES 1
#else
    #define EPIX_HAS_MODULES 0
#endif

// Compiler-specific module import/export macros
#if EPIX_HAS_MODULES
    #define EPIX_MODULE_EXPORT export
    // When using modules, use: import epix.core;
    #define EPIX_MODULE_IMPORT import
#else
    #define EPIX_MODULE_EXPORT
    // When not using modules, users should use traditional #include
    // This macro is mainly for documentation purposes
    #define EPIX_MODULE_IMPORT_NOT_SUPPORTED
#endif

// Helper macro for conditional C++23 features
#define EPIX_IF_CPP23(...) EPIX_IF_CPP23_IMPL(EPIX_HAS_CPP23, __VA_ARGS__)
#define EPIX_IF_CPP23_IMPL(cond, ...) EPIX_IF_CPP23_IMPL2(cond, __VA_ARGS__)
#define EPIX_IF_CPP23_IMPL2(cond, ...) EPIX_IF_CPP23_##cond(__VA_ARGS__)
#define EPIX_IF_CPP23_1(...) __VA_ARGS__
#define EPIX_IF_CPP23_0(...)

// Namespace for module utilities
namespace epix::module {
    // Constexpr feature flags
    inline constexpr bool has_cpp23 = EPIX_HAS_CPP23;
    inline constexpr bool has_cpp20 = EPIX_HAS_CPP20;
    inline constexpr bool has_modules = EPIX_HAS_MODULES;
    inline constexpr bool has_std_expected = EPIX_HAS_STD_EXPECTED;
    inline constexpr bool has_std_mdspan = EPIX_HAS_STD_MDSPAN;
    inline constexpr bool has_std_print = EPIX_HAS_STD_PRINT;
    inline constexpr bool has_deducing_this = EPIX_HAS_DEDUCING_THIS;
}

// Diagnostic helpers
#if EPIX_HAS_CPP23
    #pragma message("epix_engine: C++23 features enabled")
#endif

#if EPIX_HAS_MODULES
    #pragma message("epix_engine: C++20 modules enabled")
#endif

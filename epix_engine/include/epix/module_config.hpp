#pragma once

// Detect whether the current toolchain supports C++20 modules.
// We prefer the named module path when both the compiler and the standard
// library ship module support (MSVC's STL does, libstdc++/libc++ generally do not yet).
#ifndef EPIX_FORCE_TRADITIONAL
#  if defined(__cpp_modules) && (__cpp_modules >= 201907L)
#    define EPIX_HAS_MODULES 1
#  else
#    define EPIX_HAS_MODULES 0
#  endif
#else
#  define EPIX_HAS_MODULES 0
#endif

// Standard library module availability. Explicitly disable for libstdc++/libc++,
// which still lack full module coverage in many environments; MSVC's STL is the
// primary target for std module usage here.
#if defined(__cpp_lib_modules) && (__cpp_lib_modules >= 202207L)
#  define EPIX_STD_MODULES_AVAILABLE 1
#else
#  define EPIX_STD_MODULES_AVAILABLE 0
#endif

#if EPIX_STD_MODULES_AVAILABLE && !defined(_LIBCPP_VERSION) && !defined(__GLIBCXX__)
#  define EPIX_HAS_STD_MODULES 1
#else
#  define EPIX_HAS_STD_MODULES 0
#endif

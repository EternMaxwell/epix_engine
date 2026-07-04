#pragma once

#ifdef EPIX_CXX_MODULE
#define EPIX_EXPORT export
#define EPIX_EXPORT_BEGIN export {
#define EPIX_EXPORT_END }
#else
#define EPIX_EXPORT
#define EPIX_EXPORT_BEGIN
#define EPIX_EXPORT_END
#endif

#if defined(_MSC_VER)
#define EPIX_API_IMPORT __declspec(dllimport)
#define EPIX_API_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define EPIX_API_IMPORT __attribute__((visibility("default")))
#define EPIX_API_EXPORT __attribute__((visibility("default")))
#else
#define EPIX_API_IMPORT
#define EPIX_API_EXPORT
#endif

#ifdef EPIX_SHARED_LIB
#ifdef EPIX_BUILD_SHARED
#define EPIX_API EPIX_API_EXPORT
#else
#define EPIX_API EPIX_API_IMPORT
#endif
#else
#define EPIX_API
#endif

#if defined(__GNUC__) || defined(__clang__)
#define EPIX_FORCE_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#define EPIX_FORCE_INLINE __forceinline
#else
#define EPIX_FORCE_INLINE inline
#endif
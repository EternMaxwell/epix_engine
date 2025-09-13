#pragma once

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
#pragma once

#if defined(_WIN32) && defined(EPIX_BUILD_SHARED)
#define EPIX_API __declspec(dllexport)
#elif defined(_WIN32) && (defined(EPIX_DLL) || defined(EPIX_SHARED))
#define EPIX_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(EPIX_BUILD_SHARED)
#define EPIX_API __attribute__((visibility("default")))
#else
#define EPIX_API
#endif
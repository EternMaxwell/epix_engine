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

#ifndef EPIX_MAKE_U32_WRAPPER
#define EPIX_MAKE_U32_WRAPPER(type)                                              \
    struct type {                                                                \
       private:                                                                  \
        uint32_t value = 0;                                                      \
                                                                                 \
       public:                                                                   \
        constexpr type(uint32_t v = 0) : value(v) {}                             \
        constexpr uint32_t get(this const type& self) { return self.value; }     \
        constexpr void set(this type& self, uint32_t v) { self.value = v; }      \
        constexpr auto operator<=>(const type&) const = default;                 \
        constexpr operator uint32_t() const { return value; }                    \
        constexpr operator size_t() const { return static_cast<size_t>(value); } \
    };
#endif

#ifndef EPIX_MAKE_U64_WRAPPER
#define EPIX_MAKE_U64_WRAPPER(type)                                              \
    struct type {                                                                \
       private:                                                                  \
        uint64_t value = 0;                                                      \
                                                                                 \
       public:                                                                   \
        constexpr type(uint64_t v = 0) : value(v) {}                             \
        constexpr uint64_t get(this const type& self) { return self.value; }     \
        constexpr void set(this type& self, uint64_t v) { self.value = v; }      \
        constexpr auto operator<=>(const type&) const = default;                 \
        constexpr operator size_t() const { return static_cast<size_t>(value); } \
    };
#endif
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

#include "meta/typeindex.hpp"

namespace epix::core {
struct Label {
   public:
    static Label from_raw(const epix::core::meta::type_index& type_index, uintptr_t extra = 0) {
        Label label;
        label.type_index_ = type_index;
        label.extra_      = extra;
        return label;
    }
    template <typename T>
    static Label from_type() {
        return from_raw(epix::core::meta::type_id<T>());
    }
    template <typename T>
    static Label from_enum(T t)
        requires(std::is_enum_v<T>)
    {
        return from_raw(epix::core::meta::type_id<T>(), static_cast<uintptr_t>(t));
    }
    template <std::integral T>
    static Label from_integral(T value) {
        return from_raw(epix::core::meta::type_id<T>(), static_cast<uintptr_t>(value));
    }
    template <typename T>
    static Label from_pointer(T* ptr) {
        return from_raw(epix::core::meta::type_id<T>(), (uintptr_t)(ptr));
    }

    template <typename T>
    Label(T t)
        requires(!std::is_same_v<std::decay_t<T>, Label> && !std::derived_from<T, Label> &&
                 (std::is_enum_v<T> || std::is_pointer_v<T> || std::is_integral_v<T> || std::is_empty_v<T>))
    {
        if constexpr (std::is_enum_v<T>) {
            *this = Label::from_enum(t);
        } else if constexpr (std::is_pointer_v<T>) {
            *this = Label::from_pointer(t);
        } else if constexpr (std::is_integral_v<T>) {
            *this = Label::from_integral(t);
        } else if constexpr (std::is_empty_v<T>) {
            *this = Label::from_type<T>();
        }
    }
    Label() = default;

    epix::core::meta::type_index type_index() const { return type_index_; }
    uintptr_t extra() const { return extra_; }

    bool operator==(const Label& other) const noexcept = default;
    bool operator!=(const Label& other) const noexcept = default;

   protected:
    epix::core::meta::type_index type_index_;
    uintptr_t extra_ = 0;
};
};  // namespace epix::core

#ifndef EPIX_MAKE_LABEL
#define EPIX_MAKE_LABEL(type)                                                         \
    struct type : public ::epix::core::Label {                                        \
       public:                                                                        \
        type() = default;                                                             \
        template <typename T>                                                         \
        type(T t)                                                                     \
            requires(!std::is_same_v<std::decay_t<T>, type> && std::is_object_v<T> && \
                     std::constructible_from<Label, T>)                               \
            : Label(t) {}                                                             \
    };
#endif

// hash for Label
namespace std {
template <>
struct hash<epix::core::Label> {
    size_t operator()(const epix::core::Label& label) const noexcept {
        size_t hash = std::hash<size_t>()(label.type_index().hash_code());
        hash ^= std::hash<size_t>()(label.extra()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};
template <std::derived_from<epix::core::Label> T>
struct hash<T> {
    size_t operator()(const T& label) const noexcept { return std::hash<epix::core::Label>()(label); }
};
}  // namespace std
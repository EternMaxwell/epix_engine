module;

#include <concepts>
#include <cstdint>
#include <format>
#include <string>

export module epix.core:label;

import epix.meta;

namespace core {
struct Label {
   public:
    static Label from_raw(const meta::type_index& type_index, std::uintptr_t extra = 0) {
        Label label;
        label.type_index_ = type_index;
        label.extra_      = extra;
        return label;
    }
    template <typename T>
    static Label from_type() {
        return from_raw(meta::type_id<T>());
    }
    template <typename T>
    static Label from_enum(T t)
        requires(std::is_enum_v<T>)
    {
        return from_raw(meta::type_id<T>(), static_cast<std::uintptr_t>(t));
    }
    template <std::integral T>
    static Label from_integral(T value) {
        return from_raw(meta::type_id<T>(), static_cast<std::uintptr_t>(value));
    }
    template <typename T>
    static Label from_pointer(T* ptr) {
        return from_raw(meta::type_id<T>(), (std::uintptr_t)(ptr));
    }

    template <typename T>
    Label(T&& t)
        requires(!std::is_same_v<std::decay_t<T>, Label> && !std::derived_from<std::decay_t<T>, Label> &&
                 (std::is_enum_v<std::decay_t<T>> || std::is_pointer_v<std::decay_t<T>> ||
                  std::is_integral_v<std::decay_t<T>> || std::is_empty_v<std::decay_t<T>>))
    {
        if constexpr (std::is_enum_v<std::decay_t<T>>) {
            *this = Label::from_enum(t);
        } else if constexpr (std::is_pointer_v<std::decay_t<T>>) {
            *this = Label::from_pointer(t);
        } else if constexpr (std::is_integral_v<std::decay_t<T>>) {
            *this = Label::from_integral(t);
        } else if constexpr (std::is_empty_v<std::decay_t<T>>) {
            *this = Label::from_type<std::decay_t<T>>();
        }
    }
    Label() = default;

    meta::type_index type_index() const { return type_index_; }
    std::uintptr_t extra() const { return extra_; }

    std::string to_string() const { return std::format("{}#{:x}", type_index_.short_name(), extra_); }

    bool operator==(const Label& other) const noexcept = default;
    bool operator!=(const Label& other) const noexcept = default;

   protected:
    meta::type_index type_index_;
    std::uintptr_t extra_ = 0;
};
};  // namespace core

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
export template <>
struct ::std::hash<::core::Label> {
    size_t operator()(const ::core::Label& label) const noexcept {
        size_t hash = std::hash<size_t>()(label.type_index().hash_code());
        hash ^= std::hash<size_t>()(label.extra()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};
export template <std::derived_from<::core::Label> T>
struct ::std::hash<T> {
    size_t operator()(const T& label) const noexcept { return std::hash<::core::Label>()(label); }
};
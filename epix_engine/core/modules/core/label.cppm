module;

export module epix.core:label;

import std;
import epix.meta;

namespace epix::core {
/** @brief Generic label identified by a type_index and an optional extra discriminator.
 *  Used as a key for schedules, apps, and other named resources.
 *  Can be constructed from empty types, enums, integrals, or pointers. */
export struct Label {
   public:
    /** @brief Create a label from a raw type_index and optional extra discriminator. */
    static Label from_raw(const meta::type_index& type_index, std::uintptr_t extra = 0) {
        Label label;
        label.type_index_ = type_index;
        label.extra_      = extra;
        return label;
    }
    /** @brief Create a label from an empty type T. */
    template <typename T>
    static Label from_type() {
        return from_raw(meta::type_id<T>());
    }
    /** @brief Create a label from an enum value. The enum type is the base, the value is the extra. */
    template <typename T>
    static Label from_enum(T t)
        requires(std::is_enum_v<T>)
    {
        return from_raw(meta::type_id<T>(), static_cast<std::uintptr_t>(t));
    }
    /** @brief Create a label from an integral value. */
    template <std::integral T>
    static Label from_integral(T value) {
        return from_raw(meta::type_id<T>(), static_cast<std::uintptr_t>(value));
    }
    /** @brief Create a label from a typed pointer. */
    template <typename T>
    static Label from_pointer(T* ptr) {
        return from_raw(meta::type_id<T>(), (std::uintptr_t)(ptr));
    }

    /** @brief Construct a Label from an enum, pointer, integral, or empty type. */
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
    /** @brief Default-construct an empty label. */
    Label() = default;

    /** @brief Get the underlying type_index. */
    meta::type_index type_index() const { return type_index_; }
    /** @brief Get the extra discriminator value. */
    std::uintptr_t extra() const { return extra_; }

    /** @brief Get a human-readable string representation ("TypeName#hex"). */
    std::string to_string() const { return std::format("{}#{:x}", type_index_.short_name(), extra_); }

    bool operator==(const Label& other) const noexcept = default;
    bool operator!=(const Label& other) const noexcept = default;

   protected:
    /** @brief Underlying type identity. */
    meta::type_index type_index_;
    /** @brief Extra discriminator (enum value, integral, or pointer). */
    std::uintptr_t extra_ = 0;
};
};  // namespace core

// hash for Label
template <>
struct std::hash<::epix::core::Label> {
    std::size_t operator()(const ::epix::core::Label& label) const noexcept {
        std::size_t hash = std::hash<std::size_t>()(label.type_index().hash_code());
        hash ^= std::hash<std::size_t>()(label.extra()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};
template <std::derived_from<::epix::core::Label> T>
struct std::hash<T> {
    std::size_t operator()(const T& label) const noexcept {
        return std::hash<::epix::core::Label>()(static_cast<const ::epix::core::Label&>(label));
    }
};
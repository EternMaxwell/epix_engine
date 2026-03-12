module;

export module epix.utils:int_wrapper;

import std;

namespace utils {
/** @brief CRTP-style wrapper for integral types providing strong typing.
 *
 * Wraps a plain integer with value semantics, comparison operators, and
 * implicit conversions. Intended as a base for newtype integer wrappers
 * like TypeId, TableId, etc.
 * @tparam T The underlying integral type.
 */
export template <std::integral T>
struct int_base {
   public:
    using value_type = T;

    constexpr int_base() noexcept : value(0) {}
    constexpr int_base(T v) noexcept : value(v) {}
    /** @brief Get the underlying value. */
    constexpr T get(this int_base self) noexcept { return self.value; }
    /** @brief Set the underlying value. */
    constexpr void set(this int_base& self, T v) noexcept { self.value = v; }
    /** @brief Equality comparison. */
    constexpr bool operator==(const int_base& other) const noexcept = default;
    /** @brief Three-way comparison. */
    constexpr auto operator<=>(const int_base&) const noexcept = default;
    /** @brief Implicit conversion to the underlying type. */
    constexpr operator T(this const int_base self) noexcept { return self.value; }
    constexpr operator std::size_t(this const int_base self) noexcept
        requires(!std::same_as<T, std::size_t>)
    {
        return static_cast<std::size_t>(self.value);
    }

   protected:
    /** @brief Underlying integral value. */
    T value;
};
}  // namespace utils

template <std::integral T>
struct std::hash<::utils::int_base<T>> {
    std::size_t operator()(const ::utils::int_base<T>& v) const { return std::hash<T>()(v.get()); }
};
template <typename T>
    requires requires {
        typename T::value_type;
        requires std::derived_from<T, ::utils::int_base<typename T::value_type>>;
    }
struct std::hash<T> {
    std::size_t operator()(const T& v) const { return std::hash<::utils::int_base<typename T::value_type>>()(v); }
};
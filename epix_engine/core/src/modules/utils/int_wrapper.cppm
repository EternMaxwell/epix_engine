module;

#include <compare>
#include <concepts>

export module epix.core:utils.int_wrapper;

namespace core {
template <std::integral T>
struct int_base {
   public:
    using value_type = T;

    constexpr int_base() noexcept : value(0) {}
    constexpr int_base(T v) noexcept : value(v) {}
    constexpr T get(this int_base self) noexcept { return self.value; }
    constexpr void set(this int_base& self, T v) noexcept { self.value = v; }
    constexpr bool operator==(const int_base& other) const noexcept = default;
    constexpr auto operator<=>(const int_base&) const noexcept      = default;
    constexpr operator T(this const int_base self) noexcept { return self.value; }
    constexpr operator size_t(this const int_base self) noexcept
        requires(!std::same_as<T, size_t>)
    {
        return static_cast<size_t>(self.value);
    }

   protected:
    T value;
};
}  // namespace core

export template <std::integral T>
struct ::std::hash<::core::int_base<T>> {
    size_t operator()(const ::core::int_base<T>& v) const { return std::hash<T>()(v.get()); }
};
export template <typename T>
    requires requires {
        typename T::value_type;
        requires std::derived_from<T, ::core::int_base<typename T::value_type>>;
    }
struct ::std::hash<T> {
    size_t operator()(const T& v) const { return std::hash<::core::int_base<typename T::value_type>>()(v); }
};
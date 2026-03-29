export module epix.utils:fixed_point;

import std;

namespace epix::utils {
export template <std::size_t IntBits>
    requires(IntBits > 0 && IntBits < 32)
struct fixed32 {
    std::int32_t value{};

    static constexpr std::size_t FracBits = 32 - IntBits;
    static constexpr std::int64_t scale   = std::int64_t(1) << FracBits;
    static constexpr std::int64_t mask    = scale - 1;

    constexpr fixed32() = default;
    explicit constexpr fixed32(float f) : value(static_cast<std::int32_t>(std::round(f * scale))) {}
    explicit constexpr fixed32(double d) : value(static_cast<std::int32_t>(std::round(d * scale))) {}
    explicit constexpr fixed32(std::int32_t raw_value) : value(raw_value) {}

    explicit constexpr operator float() const { return static_cast<float>(value) / scale; }
    explicit constexpr operator double() const { return static_cast<double>(value) / scale; }

    constexpr fixed32 operator+(const fixed32& rhs) const { return fixed32(value + rhs.value); }
    constexpr fixed32 operator-(const fixed32& rhs) const { return fixed32(value - rhs.value); }
    constexpr fixed32 operator*(const fixed32& rhs) const {
        std::int64_t temp = static_cast<std::int64_t>(value) * rhs.value;
        return fixed32(static_cast<std::int32_t>((temp + (scale / 2)) >> FracBits));
    }
    constexpr fixed32 operator/(const fixed32& rhs) const {
        std::int64_t temp = (static_cast<std::int64_t>(value) << FracBits) / rhs.value;
        return fixed32(static_cast<std::int32_t>(temp));
    }

    constexpr fixed32& operator+=(const fixed32& rhs) {
        value += rhs.value;
        return *this;
    }
    constexpr fixed32& operator-=(const fixed32& rhs) {
        value -= rhs.value;
        return *this;
    }
    constexpr fixed32& operator*=(const fixed32& rhs) {
        std::int64_t temp = static_cast<std::int64_t>(value) * rhs.value;
        value             = static_cast<std::int32_t>((temp + (scale / 2)) >> FracBits);
        return *this;
    }
    constexpr fixed32& operator/=(const fixed32& rhs) {
        std::int64_t temp = (static_cast<std::int64_t>(value) << FracBits) / rhs.value;
        value             = static_cast<std::int32_t>(temp);
        return *this;
    }
};

export template <std::size_t IntBits>
    requires(IntBits > 0 && IntBits < 64)
struct fixed64 {
    std::int64_t value{};

    static constexpr std::size_t FracBits = 64 - IntBits;
    static constexpr std::int64_t scale   = std::int64_t(1) << FracBits;
    static constexpr std::int64_t mask    = scale - 1;

    constexpr fixed64() = default;
    explicit constexpr fixed64(float f) : value(static_cast<std::int64_t>(std::round(f * scale))) {}
    explicit constexpr fixed64(double d) : value(static_cast<std::int64_t>(std::round(d * scale))) {}
    explicit constexpr fixed64(std::int64_t raw_value) : value(raw_value) {}

    explicit constexpr operator float() const { return static_cast<float>(value) / scale; }
    explicit constexpr operator double() const { return static_cast<double>(value) / scale; }

    constexpr fixed64 operator+(const fixed64& rhs) const { return fixed64(value + rhs.value); }
    constexpr fixed64 operator-(const fixed64& rhs) const { return fixed64(value - rhs.value); }
    constexpr fixed64 operator*(const fixed64& rhs) const {
        std::uint64_t mask32 = (std::uint64_t(1) << 32) - 1;
        bool sign            = (value < 0) ^ (rhs.value < 0);
        std::uint64_t a      = value < 0 ? -value : value;
        std::uint64_t b      = rhs.value < 0 ? -rhs.value : rhs.value;
        // use 32 bit mask to split the value into high and low parts to avoid overflow
        // then we will adjust it to the correct scale after multiplication

        std::uint64_t a_lo = a & mask32;
        std::uint64_t a_hi = a >> 32;
        std::uint64_t b_lo = b & mask32;
        std::uint64_t b_hi = b >> 32;

        // we should have a 128bit result, but splited into 3 parts: hh, hl and ll. hh is the high 64 bit, hl is the
        // middle 64 bit and ll is the low 64 bit. we will add them together with the correct shift and then adjust it
        // to the correct scale.
        std::uint64_t hh = a_hi * b_hi;
        std::uint64_t hl = a_hi * b_lo + a_lo * b_hi;
        std::uint64_t ll = a_lo * b_lo + scale / 2;  // add scale/2 for rounding
        if (ll < scale / 2) hl += 1;

        std::uint64_t tmp = (hh << (64 - FracBits)) + (hl << (32 - FracBits)) + (ll >> FracBits);
        return fixed64(sign ? -static_cast<std::int64_t>(tmp) : static_cast<std::int64_t>(tmp));
    }
    constexpr fixed64 operator/(const fixed64& rhs) const {
        bool sign       = (value < 0) ^ (rhs.value < 0);
        std::uint64_t a = value < 0 ? -value : value;
        std::uint64_t b = rhs.value < 0 ? -rhs.value : rhs.value;

        std::uint64_t low  = (a << FracBits);
        std::uint64_t high = a >> (64 - FracBits);

        std::uint64_t quotient = 0;
        // restoring long division: shift the 128-bit numerator left one bit
        // then compare/subtract the divisor and set the quotient bit.
        for (int i = 0; i < 64; ++i) {
            high = (high << 1) | (low >> 63);
            low <<= 1;
            quotient <<= 1;
            if (high >= b) {
                high -= b;
                quotient |= 1;
            }
        }
        return fixed64(sign ? -static_cast<std::int64_t>(quotient) : static_cast<std::int64_t>(quotient));
    }

    constexpr fixed64& operator+=(const fixed64& rhs) {
        value += rhs.value;
        return *this;
    }
    constexpr fixed64& operator-=(const fixed64& rhs) {
        value -= rhs.value;
        return *this;
    }
    constexpr fixed64& operator*=(const fixed64& rhs) {
        *this = *this * rhs;
        return *this;
    }
    constexpr fixed64& operator/=(const fixed64& rhs) {
        *this = *this / rhs;
        return *this;
    }
};
}  // namespace utils
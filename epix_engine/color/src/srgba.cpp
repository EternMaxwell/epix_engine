#include <epix/color/srgba.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/xyza.hpp>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace epix::color {
const Srgba Srgba::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Srgba Srgba::NONE(0.0f, 0.0f, 0.0f, 0.0f);
const Srgba Srgba::WHITE(1.0f, 1.0f, 1.0f, 1.0f);
const Srgba Srgba::RED(1.0f, 0.0f, 0.0f, 1.0f);
const Srgba Srgba::GREEN(0.0f, 1.0f, 0.0f, 1.0f);
const Srgba Srgba::BLUE(0.0f, 0.0f, 1.0f, 1.0f);
EPIX_COLOR_DEFINE_ZERO(Srgba)

namespace {

/** @brief Bevy's `round(v.clamp(0.0, 1.0) * 255.0) as u8` (`srgba.rs:376`). */
std::uint8_t channel_to_u8(float value) {
    return static_cast<std::uint8_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

/** @brief `u16/u32::from_str_radix(hex, 16)` (`srgba.rs:146`): every character must be an ASCII
 *  hex digit, and the value is accumulated base 16.
 *
 *  Rust's `ParseIntError` has no payload the port can carry, so the invalid character is reported
 *  as `HexColorError::Kind::Char` together with the offending character. Overflow is impossible:
 *  the caller only parses the four accepted lengths, of which the longest is 8 digits. */
std::expected<std::uint32_t, HexColorError> parse_hex_digits(std::string_view hex) {
    std::uint32_t value = 0;
    for (const char character : hex) {
        std::uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint32_t>(character - 'a') + 10u;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A') + 10u;
        } else {
            return std::unexpected(HexColorError{HexColorError::Kind::Char, character});
        }
        value = value * 16u + digit;
    }
    return value;
}

}  // namespace

float Srgba::gamma_function(float value) {
    if (value <= 0.0f) {
        return value;
    }
    if (value <= 0.04045f) {
        return value / 12.92f;  // linear falloff in dark values
    }
    return std::pow((value + 0.055f) / 1.055f, 2.4f);  // gamma curve in other area
}

float Srgba::gamma_function_inverse(float value) {
    if (value <= 0.0f) {
        return value;
    }
    if (value <= 0.0031308f) {
        return value * 12.92f;  // linear falloff in dark values
    }
    return (1.055f * std::pow(value, 1.0f / 2.4f)) - 0.055f;  // gamma curve in other area
}

std::expected<Srgba, HexColorError> Srgba::hex(std::string_view hex) {
    // Bevy does `hex.strip_prefix('#').unwrap_or(hex)` (`srgba.rs:141`).
    if (hex.starts_with('#')) {
        hex.remove_prefix(1);
    }

    // Bevy matches on the length first (`srgba.rs:143-172`), so a wrong digit count is `Length`
    // even when the characters are not hex digits either.
    switch (hex.size()) {
    case 3:
    case 4:
    case 6:
    case 8:
        break;
    default:
        return std::unexpected(HexColorError{HexColorError::Kind::Length});
    }

    const std::expected<std::uint32_t, HexColorError> digits = parse_hex_digits(hex);
    if (!digits) {
        return std::unexpected(digits.error());
    }

    // Bevy destructures `to_be_bytes()` of the parsed value; the 3- and 4-digit forms parse into
    // a `u16` and so read the two low bytes, the 6- and 8-digit forms all four.
    const std::uint8_t byte0 = static_cast<std::uint8_t>((*digits >> 24) & 0xFFu);
    const std::uint8_t byte1 = static_cast<std::uint8_t>((*digits >> 16) & 0xFFu);
    const std::uint8_t byte2 = static_cast<std::uint8_t>((*digits >> 8) & 0xFFu);
    const std::uint8_t byte3 = static_cast<std::uint8_t>(*digits & 0xFFu);

    switch (hex.size()) {
    case 3: {
        // RGB: each nibble is replicated into its channel.
        const std::uint8_t r = byte2 & 0x0Fu;
        const std::uint8_t g = (byte3 & 0xF0u) >> 4;
        const std::uint8_t b = byte3 & 0x0Fu;
        return rgb_u8(static_cast<std::uint8_t>((r << 4) | r), static_cast<std::uint8_t>((g << 4) | g),
                      static_cast<std::uint8_t>((b << 4) | b));
    }
    case 4: {
        // RGBA
        const std::uint8_t r = (byte2 & 0xF0u) >> 4;
        const std::uint8_t g = byte2 & 0x0Fu;
        const std::uint8_t b = (byte3 & 0xF0u) >> 4;
        const std::uint8_t a = byte3 & 0x0Fu;
        return rgba_u8(static_cast<std::uint8_t>((r << 4) | r), static_cast<std::uint8_t>((g << 4) | g),
                       static_cast<std::uint8_t>((b << 4) | b), static_cast<std::uint8_t>((a << 4) | a));
    }
    case 6:
        // RRGGBB
        return rgb_u8(byte1, byte2, byte3);
    default:
        // RRGGBBAA (the only remaining accepted length)
        return rgba_u8(byte0, byte1, byte2, byte3);
    }
}

std::string Srgba::hex() const {
    const std::array<std::uint8_t, 4> bytes = u8_array();
    const unsigned int r                    = bytes[0];
    const unsigned int g                    = bytes[1];
    const unsigned int b                    = bytes[2];
    const unsigned int a                    = bytes[3];
    if (a == 255u) {
        return std::format("#{:02X}{:02X}{:02X}", r, g, b);
    }
    return std::format("#{:02X}{:02X}{:02X}{:02X}", r, g, b, a);
}

std::array<std::uint8_t, 4> Srgba::u8_array() const {
    return {channel_to_u8(red), channel_to_u8(green), channel_to_u8(blue), channel_to_u8(alpha)};
}

std::array<std::uint8_t, 3> Srgba::u8_array_no_alpha() const {
    return {channel_to_u8(red), channel_to_u8(green), channel_to_u8(blue)};
}

Srgba::Srgba(const std::array<std::uint8_t, 4>& color)
    : Srgba(static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f,
            static_cast<float>(color[2]) / 255.0f, static_cast<float>(color[3]) / 255.0f) {}

Srgba::Srgba(const std::array<std::uint8_t, 3>& color)
    : Srgba(static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f,
            static_cast<float>(color[2]) / 255.0f, 1.0f) {}

float Srgba::luminance() const {
    const LinearRgba linear = LinearRgba(*this);
    return linear.luminance();
}

Srgba Srgba::with_luminance(float luminance) const {
    const LinearRgba linear = LinearRgba(*this);
    return Srgba(linear.with_luminance(Srgba::gamma_function(luminance)));
}

Srgba Srgba::darker(float amount) const {
    const LinearRgba linear = LinearRgba(*this);
    return Srgba(linear.darker(amount));
}

Srgba Srgba::lighter(float amount) const {
    const LinearRgba linear = LinearRgba(*this);
    return Srgba(linear.lighter(amount));
}

Srgba Srgba::mix(const Srgba& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Srgba(red * n_factor + other.red * factor, green * n_factor + other.green * factor,
                 blue * n_factor + other.blue * factor, alpha * n_factor + other.alpha * factor);
}

float Srgba::distance_squared(const Srgba& other) const {
    const float dr = red - other.red;
    const float dg = green - other.green;
    const float db = blue - other.blue;
    return dr * dr + dg * dg + db * db;
}

Srgba::Srgba(const LinearRgba& value)
    : Srgba(Srgba::gamma_function_inverse(value.red), Srgba::gamma_function_inverse(value.green),
            Srgba::gamma_function_inverse(value.blue), value.alpha) {}

// Derived Conversions

Srgba::Srgba(const Xyza& value) : Srgba(LinearRgba(value)) {}

Srgba::Srgba(const Hsla& value) : Srgba(Hsva(value)) {}

Srgba::Srgba(const Hsva& value) : Srgba(Hwba(value)) {}

Srgba::Srgba(const Hwba& value) : Srgba(0.0f, 0.0f, 0.0f, value.alpha) {
    // Based on "HWB - A More Intuitive Hue-Based Color Model" Appendix B
    const float w = value.whiteness;
    const float v = 1.0f - value.blackness;

    // Rust's `%` keeps the sign of the dividend, so this is `fmod`, not `rem_euclid`.
    const float h = std::fmod(value.hue, 360.0f) / 60.0f;
    const float i = std::floor(h);
    float f       = h - i;

    // Rust's `i as u8` saturates, so a negative `floor` lands on the index-0 arm.
    const std::uint8_t index = static_cast<std::uint8_t>(std::clamp(i, 0.0f, 255.0f));

    if (index % 2 != 0) {
        f = 1.0f - f;
    }

    const float n = w + f * (v - w);

    switch (index) {
    case 0:
        red = v;
        green = n;
        blue = w;
        break;
    case 1:
        red = n;
        green = v;
        blue = w;
        break;
    case 2:
        red = w;
        green = v;
        blue = n;
        break;
    case 3:
        red = w;
        green = n;
        blue = v;
        break;
    case 4:
        red = n;
        green = w;
        blue = v;
        break;
    case 5:
        red = v;
        green = w;
        blue = n;
        break;
    default:
        // `i` is bounded in [0, 6), so this arm is unreachable, as in Bevy.
        red = v;
        green = n;
        blue = w;
        break;
    }
}

Srgba::Srgba(const Laba& value) : Srgba(Xyza(value)) {}

Srgba::Srgba(const Lcha& value) : Srgba(Laba(value)) {}

Srgba::Srgba(const Oklaba& value) : Srgba(LinearRgba(value)) {}

Srgba::Srgba(const Oklcha& value) : Srgba(Oklaba(value)) {}

Srgba::Srgba(const Color& value)
    : Srgba(std::visit(
          [](const auto& color) -> Srgba {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Srgba>) {
                  return color;
              } else {
                  return Srgba(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

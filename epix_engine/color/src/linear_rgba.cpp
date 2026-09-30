#include <epix/color/linear_rgba.hpp>
#include <algorithm>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>
#include <type_traits>
#include <variant>

namespace epix::color {
const LinearRgba LinearRgba::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const LinearRgba LinearRgba::WHITE(1.0f, 1.0f, 1.0f, 1.0f);
const LinearRgba LinearRgba::NONE(0.0f, 0.0f, 0.0f, 0.0f);
const LinearRgba LinearRgba::RED(1.0f, 0.0f, 0.0f, 1.0f);
const LinearRgba LinearRgba::GREEN(0.0f, 1.0f, 0.0f, 1.0f);
const LinearRgba LinearRgba::BLUE(0.0f, 0.0f, 1.0f, 1.0f);
const LinearRgba LinearRgba::NaN{std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::quiet_NaN()};
EPIX_COLOR_DEFINE_ZERO(LinearRgba)

namespace {

/** @brief Bevy's `round(v.clamp(0.0, 1.0) * 255.0) as u8` (`linear_rgba.rs:309`). */
std::uint8_t channel_to_u8(float value) {
    return static_cast<std::uint8_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

LinearRgba::LinearRgba(const std::array<std::uint8_t, 4>& color)
    : LinearRgba(static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f,
                 static_cast<float>(color[2]) / 255.0f, static_cast<float>(color[3]) / 255.0f) {}

LinearRgba::LinearRgba(const std::array<std::uint8_t, 3>& color)
    : LinearRgba(static_cast<float>(color[0]) / 255.0f, static_cast<float>(color[1]) / 255.0f,
                 static_cast<float>(color[2]) / 255.0f, 1.0f) {}

LinearRgba::LinearRgba(const Srgba& value)
    : LinearRgba(Srgba::gamma_function(value.red), Srgba::gamma_function(value.green),
                 Srgba::gamma_function(value.blue), value.alpha) {}

LinearRgba::LinearRgba(const Xyza& value) : LinearRgba(0.0f, 0.0f, 0.0f, value.alpha) {
    // XYZ to Linear sRGB
    // http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_RGB.html
    // http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html (sRGB, XYZ to RGB [M]-1)
    red   = value.x * 3.2404542f + value.y * -1.5371385f + value.z * -0.4985314f;
    green = value.x * -0.969266f + value.y * 1.8760108f + value.z * 0.041556f;
    blue  = value.x * 0.0556434f + value.y * -0.2040259f + value.z * 1.0572252f;
}

LinearRgba::LinearRgba(const Hsla& value) : LinearRgba(Hsva(value)) {}

LinearRgba::LinearRgba(const Hsva& value) : LinearRgba(Hwba(value)) {}

LinearRgba::LinearRgba(const Hwba& value) : LinearRgba(Srgba(value)) {}

LinearRgba::LinearRgba(const Laba& value) : LinearRgba(Xyza(value)) {}

LinearRgba::LinearRgba(const Lcha& value) : LinearRgba(Laba(value)) {}

LinearRgba::LinearRgba(const Oklaba& value) : LinearRgba(0.0f, 0.0f, 0.0f, value.alpha) {
    // From https://bottosson.github.io/posts/oklab/#converting-from-linear-srgb-to-oklab
    // Float literals are truncated to avoid excessive precision.
    const float l_ = value.lightness + 0.39633778f * value.a + 0.21580376f * value.b;
    const float m_ = value.lightness - 0.105561346f * value.a - 0.06385417f * value.b;
    const float s_ = value.lightness - 0.08948418f * value.a - 1.2914855f * value.b;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    red   = 4.0767417f * l - 3.3077116f * m + 0.23096994f * s;
    green = -1.268438f * l + 2.6097574f * m - 0.34131938f * s;
    blue  = -0.0041960863f * l - 0.7034186f * m + 1.7076147f * s;
}

LinearRgba::LinearRgba(const Oklcha& value) : LinearRgba(Oklaba(value)) {}

LinearRgba::LinearRgba(const Color& value)
    : LinearRgba(std::visit(
          [](const auto& color) -> LinearRgba {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, LinearRgba>) {
                  return color;
              } else {
                  return LinearRgba(color);
              }
          },
          value.color)) {}

std::uint32_t LinearRgba::as_u32() const {
    const std::array<std::uint8_t, 4> bytes = u8_array();
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

float LinearRgba::luminance() const { return red * 0.2126f + green * 0.7152f + blue * 0.0722f; }

LinearRgba LinearRgba::with_luminance(float luminance) const {
    const float current_luminance = this->luminance();
    const float adjustment        = luminance / current_luminance;
    return LinearRgba(std::clamp(red * adjustment, 0.0f, 1.0f), std::clamp(green * adjustment, 0.0f, 1.0f),
                      std::clamp(blue * adjustment, 0.0f, 1.0f), alpha);
}

void LinearRgba::adjust_lightness(float amount) {
    const float luminance        = this->luminance();
    const float target_luminance = std::clamp(luminance + amount, 0.0f, 1.0f);
    if (target_luminance < luminance) {
        const float adjustment = (luminance - target_luminance) / luminance;
        mix_assign(LinearRgba(0.0f, 0.0f, 0.0f, alpha), adjustment);
    } else if (target_luminance > luminance) {
        const float adjustment = (target_luminance - luminance) / (1.0f - luminance);
        mix_assign(LinearRgba(1.0f, 1.0f, 1.0f, alpha), adjustment);
    }
}

LinearRgba LinearRgba::darker(float amount) const {
    LinearRgba result = *this;
    result.adjust_lightness(-amount);
    return result;
}

LinearRgba LinearRgba::lighter(float amount) const {
    LinearRgba result = *this;
    result.adjust_lightness(amount);
    return result;
}

LinearRgba LinearRgba::mix(const LinearRgba& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return LinearRgba(red * n_factor + other.red * factor, green * n_factor + other.green * factor,
                      blue * n_factor + other.blue * factor, alpha * n_factor + other.alpha * factor);
}

float LinearRgba::distance_squared(const LinearRgba& other) const {
    const float dr = red - other.red;
    const float dg = green - other.green;
    const float db = blue - other.blue;
    return dr * dr + dg * dg + db * db;
}

std::array<std::uint8_t, 4> LinearRgba::u8_array() const {
    return {channel_to_u8(red), channel_to_u8(green), channel_to_u8(blue), channel_to_u8(alpha)};
}

std::array<std::uint8_t, 3> LinearRgba::u8_array_no_alpha() const {
    return {channel_to_u8(red), channel_to_u8(green), channel_to_u8(blue)};
}

}  // namespace epix::color

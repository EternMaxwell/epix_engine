#include <epix/color/oklcha.hpp>

#include <cmath>
#include <type_traits>
#include <variant>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>

namespace epix::color {
const Oklcha Oklcha::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Oklcha Oklcha::WHITE(1.0f, 0.000000059604645f, 90.0f, 1.0f);

namespace {

/** @brief Rust's `f32::to_degrees` multiplier, the literal `57.2957795130823208767981548141051703`
 *  truncated to `f32`. Bevy converts through this constant rather than through a reciprocal, and
 *  it is one ULP away from `180.0f / std::numbers::pi_v<float>`, so the constant is spelled out to
 *  keep the `Oklaba` → `Oklcha` hue identical to Bevy's. */
constexpr float RADS_TO_DEGREES = 57.2957795130823208767981548141051703f;

}  // namespace

Oklcha Oklcha::mix(const Oklcha& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Oklcha(lightness * n_factor + other.lightness * factor,
                  chroma * n_factor + other.chroma * factor, lerp_hue(hue, other.hue, factor),
                  alpha * n_factor + other.alpha * factor);
}

float Oklcha::distance_squared(const Oklcha& other) const {
    const float dl = lightness - other.lightness;
    const float dc = chroma - other.chroma;
    const float dh = hue - other.hue;
    return dl * dl + dc * dc + dh * dh;
}

Oklcha::Oklcha(const Oklaba& value) : Oklcha(value.lightness, 0.0f, 0.0f, value.alpha) {
    // The rectangular → polar half. Bevy spells the angle step `atan2(b, a).to_degrees()` and then
    // wraps the negative result into [0, 360), the only branch in either of these two types.
    const float hue_degrees = std::atan2(value.b, value.a) * RADS_TO_DEGREES;

    chroma = std::hypot(value.a, value.b);
    hue    = hue_degrees < 0.0f ? hue_degrees + 360.0f : hue_degrees;
}

// Derived conversions (Bevy hops every one of them through `Oklaba`).

Oklcha::Oklcha(const Hsla& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Hsva& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Hwba& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Laba& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Lcha& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const LinearRgba& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Srgba& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Xyza& value) : Oklcha(Oklaba(value)) {}

Oklcha::Oklcha(const Color& value)
    : Oklcha(std::visit(
          [](const auto& color) -> Oklcha {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Oklcha>) {
                  return color;
              } else {
                  return Oklcha(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

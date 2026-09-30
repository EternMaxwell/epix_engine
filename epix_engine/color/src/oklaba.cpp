#include <epix/color/oklaba.hpp>

#include <cmath>
#include <numbers>
#include <type_traits>
#include <variant>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>

namespace epix::color {
const Oklaba Oklaba::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Oklaba Oklaba::WHITE(1.0f, 0.0f, 0.000000059604645f, 1.0f);
EPIX_COLOR_DEFINE_ZERO(Oklaba)

namespace {

/** @brief Rust's `f32::to_radians` multiplier, `core::f32::consts::PI / 180.0` truncated to
 *  `f32`; `std::numbers::pi_v<float> / 180.0f` is the identical float, so the degrees → radians
 *  half of the `Oklaba`/`Oklcha` pair matches Bevy bit for bit. */
constexpr float RADS_PER_DEG = std::numbers::pi_v<float> / 180.0f;

}  // namespace

Oklaba Oklaba::mix(const Oklaba& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Oklaba(lightness * n_factor + other.lightness * factor, a * n_factor + other.a * factor,
                  b * n_factor + other.b * factor, alpha * n_factor + other.alpha * factor);
}

float Oklaba::distance_squared(const Oklaba& other) const {
    const float dl = lightness - other.lightness;
    const float da = a - other.a;
    const float db = b - other.b;
    return dl * dl + da * da + db * db;
}

Oklaba::Oklaba(const LinearRgba& value) : Oklaba(0.0f, 0.0f, 0.0f, value.alpha) {
    // From https://bottosson.github.io/posts/oklab/#converting-from-linear-srgb-to-oklab
    // Float literals are truncated to avoid excessive precision.
    const float l = 0.41222146f * value.red + 0.53633255f * value.green + 0.051445995f * value.blue;
    const float m = 0.2119035f * value.red + 0.6806995f * value.green + 0.10739696f * value.blue;
    const float s = 0.08830246f * value.red + 0.28171885f * value.green + 0.6299787f * value.blue;
    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);

    lightness = 0.21045426f * l_ + 0.7936178f * m_ - 0.004072047f * s_;
    a         = 1.9779985f * l_ - 2.4285922f * m_ + 0.4505937f * s_;
    b         = 0.025904037f * l_ + 0.78277177f * m_ - 0.80867577f * s_;
}

Oklaba::Oklaba(const Oklcha& value) : Oklaba(value.lightness, 0.0f, 0.0f, value.alpha) {
    // The polar → rectangular half. Bevy spells the angle step `hue.to_radians()`.
    const float radians = value.hue * RADS_PER_DEG;

    a = value.chroma * std::cos(radians);
    b = value.chroma * std::sin(radians);
}

// Derived conversions (Bevy's own delegation order).

Oklaba::Oklaba(const Hsla& value) : Oklaba(LinearRgba(value)) {}

Oklaba::Oklaba(const Hsva& value) : Oklaba(LinearRgba(value)) {}

Oklaba::Oklaba(const Hwba& value) : Oklaba(LinearRgba(value)) {}

Oklaba::Oklaba(const Lcha& value) : Oklaba(LinearRgba(value)) {}

Oklaba::Oklaba(const Srgba& value) : Oklaba(LinearRgba(value)) {}

Oklaba::Oklaba(const Xyza& value) : Oklaba(LinearRgba(value)) {}

Oklaba::Oklaba(const Laba& value) : Oklaba(Xyza(value)) {}

Oklaba::Oklaba(const Color& value)
    : Oklaba(std::visit(
          [](const auto& color) -> Oklaba {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Oklaba>) {
                  return color;
              } else {
                  return Oklaba(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

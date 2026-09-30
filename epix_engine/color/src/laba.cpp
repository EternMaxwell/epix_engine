#include <epix/color/laba.hpp>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <variant>

namespace epix::color {
const Laba Laba::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Laba Laba::WHITE(1.0f, 0.0f, 0.0f, 1.0f);
EPIX_COLOR_DEFINE_ZERO(Laba)

namespace {

/** @brief Rust's `f32::to_radians`, i.e. the `RADS_PER_DEG` constant it folds from `consts::PI`
 *  (`laba.rs` only needs it for the polar `Lcha` → `Laba` conversion, `lcha.rs:264`). The f64
 *  division narrows to the same `0.017453292f` Rust's crate uses. */
float to_radians(float degrees) { return degrees * static_cast<float>(std::numbers::pi / 180.0); }

}  // namespace

Laba Laba::mix(const Laba& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Laba(lightness * n_factor + other.lightness * factor, a * n_factor + other.a * factor,
                b * n_factor + other.b * factor, alpha * n_factor + other.alpha * factor);
}

Laba::Laba(const Xyza& value) : Laba(0.0f, 0.0f, 0.0f, value.alpha) {
    // Based on http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_Lab.html
    const float xr = value.x / Xyza::D65_WHITE.x;
    const float yr = value.y / Xyza::D65_WHITE.y;
    const float zr = value.z / Xyza::D65_WHITE.z;
    const float fx = xr > Laba::CIE_EPSILON ? std::cbrt(xr) : (Laba::CIE_KAPPA * xr + 16.0f) / 116.0f;
    const float fy = yr > Laba::CIE_EPSILON ? std::cbrt(yr) : (Laba::CIE_KAPPA * yr + 16.0f) / 116.0f;
    const float fz = zr > Laba::CIE_EPSILON ? std::cbrt(zr) : (Laba::CIE_KAPPA * zr + 16.0f) / 116.0f;

    lightness = 1.16f * fy - 0.16f;
    a         = 5.00f * (fx - fy);
    b         = 2.00f * (fy - fz);
}

Laba::Laba(const Srgba& value) : Laba(Xyza(value)) {}

Laba::Laba(const LinearRgba& value) : Laba(Xyza(value)) {}

Laba::Laba(const Hsla& value) : Laba(Xyza(value)) {}

Laba::Laba(const Hsva& value) : Laba(Xyza(value)) {}

Laba::Laba(const Hwba& value) : Laba(Xyza(value)) {}

Laba::Laba(const Oklaba& value) : Laba(Xyza(value)) {}

Laba::Laba(const Oklcha& value) : Laba(Oklaba(value)) {}

Laba::Laba(const Lcha& value) : Laba(0.0f, 0.0f, 0.0f, value.alpha) {
    // Based on http://www.brucelindbloom.com/index.html?Eqn_LCH_to_Lab.html
    lightness = value.lightness;
    a         = value.chroma * std::cos(to_radians(value.hue));
    b         = value.chroma * std::sin(to_radians(value.hue));
}

Laba::Laba(const Color& value)
    : Laba(std::visit(
          [](const auto& color) -> Laba {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Laba>) {
                  return color;
              } else {
                  return Laba(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

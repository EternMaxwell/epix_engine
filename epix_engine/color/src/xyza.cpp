#include <epix/color/xyza.hpp>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <cmath>
#include <type_traits>
#include <variant>

namespace epix::color {
const Xyza Xyza::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Xyza Xyza::WHITE(0.95047f, 1.0f, 1.08883f, 1.0f);
const Xyza Xyza::D65_WHITE(0.95047f, 1.0f, 1.08883f, 1.0f);
EPIX_COLOR_DEFINE_ZERO(Xyza)

Xyza Xyza::mix(const Xyza& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Xyza(x * n_factor + other.x * factor, y * n_factor + other.y * factor,
                z * n_factor + other.z * factor, alpha * n_factor + other.alpha * factor);
}

Xyza::Xyza(const LinearRgba& value) : Xyza(0.0f, 0.0f, 0.0f, value.alpha) {
    // Linear sRGB to XYZ
    // http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_RGB.html
    // http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html (sRGB, RGB to XYZ [M])
    const float r = value.red;
    const float g = value.green;
    const float b = value.blue;

    x = r * 0.4124564f + g * 0.3575761f + b * 0.1804375f;
    y = r * 0.2126729f + g * 0.7151522f + b * 0.072175f;
    z = r * 0.0193339f + g * 0.119192f + b * 0.9503041f;
}

Xyza::Xyza(const Srgba& value) : Xyza(LinearRgba(value)) {}

Xyza::Xyza(const Hsla& value) : Xyza(Hsva(value)) {}

Xyza::Xyza(const Hsva& value) : Xyza(Hwba(value)) {}

Xyza::Xyza(const Hwba& value) : Xyza(Srgba(value)) {}

Xyza::Xyza(const Laba& value) : Xyza(0.0f, 0.0f, 0.0f, value.alpha) {
    // Based on http://www.brucelindbloom.com/index.html?Eqn_Lab_to_XYZ.html
    const float l = 100.0f * value.lightness;
    const float a = 100.0f * value.a;
    const float b = 100.0f * value.b;

    const float fy = (l + 16.0f) / 116.0f;
    const float fx = a / 500.0f + fy;
    const float fz = fy - b / 200.0f;
    const float fx3 = std::pow(fx, 3.0f);
    const float xr  = fx3 > Laba::CIE_EPSILON ? fx3 : (116.0f * fx - 16.0f) / Laba::CIE_KAPPA;
    const float yr  = l > Laba::CIE_EPSILON * Laba::CIE_KAPPA ? std::pow((l + 16.0f) / 116.0f, 3.0f)
                                                              : l / Laba::CIE_KAPPA;
    const float fz3 = std::pow(fz, 3.0f);
    const float zr  = fz3 > Laba::CIE_EPSILON ? fz3 : (116.0f * fz - 16.0f) / Laba::CIE_KAPPA;

    x = xr * Xyza::D65_WHITE.x;
    y = yr * Xyza::D65_WHITE.y;
    z = zr * Xyza::D65_WHITE.z;
}

Xyza::Xyza(const Lcha& value) : Xyza(Laba(value)) {}

Xyza::Xyza(const Oklaba& value) : Xyza(LinearRgba(value)) {}

Xyza::Xyza(const Oklcha& value) : Xyza(Oklaba(value)) {}

Xyza::Xyza(const Color& value)
    : Xyza(std::visit(
          [](const auto& color) -> Xyza {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Xyza>) {
                  return color;
              } else {
                  return Xyza(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

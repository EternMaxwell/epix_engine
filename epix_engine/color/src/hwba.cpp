#include <epix/color/hwba.hpp>
#include <algorithm>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>
#include <type_traits>
#include <variant>

namespace epix::color {
const Hwba Hwba::BLACK(0.0f, 0.0f, 1.0f, 1.0f);
const Hwba Hwba::WHITE(0.0f, 1.0f, 0.0f, 1.0f);

Hwba Hwba::mix(const Hwba& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Hwba(lerp_hue(hue, other.hue, factor), whiteness * n_factor + other.whiteness * factor,
                blackness * n_factor + other.blackness * factor, alpha * n_factor + other.alpha * factor);
}

Hwba::Hwba(const Srgba& value) : Hwba(0.0f, 0.0f, 0.0f, value.alpha) {
    // Based on "HWB - A More Intuitive Hue-Based Color Model" Appendix B
    const float x_max = std::max(std::max(std::max(0.0f, value.red), value.green), value.blue);
    const float x_min = std::min(std::min(std::min(1.0f, value.red), value.green), value.blue);

    const float chroma = x_max - x_min;

    if (chroma == 0.0f) {
        hue = 0.0f;
    } else if (value.red == x_max) {
        hue = 60.0f * (value.green - value.blue) / chroma;
    } else if (value.green == x_max) {
        hue = 60.0f * (2.0f + (value.blue - value.red) / chroma);
    } else {
        hue = 60.0f * (4.0f + (value.red - value.green) / chroma);
    }
    if (hue < 0.0f) {
        hue = 360.0f + hue;
    }

    whiteness = x_min;
    blackness = 1.0f - x_max;
}

Hwba::Hwba(const Xyza& value) : Hwba(Srgba(value)) {}

Hwba::Hwba(const LinearRgba& value) : Hwba(Srgba(value)) {}

Hwba::Hwba(const Hsla& value) : Hwba(Hsva(value)) {}

Hwba::Hwba(const Hsva& value) : Hwba(value.hue, 0.0f, 0.0f, value.alpha) {
    // Based on https://en.wikipedia.org/wiki/HWB_color_model#Conversion
    whiteness = (1.0f - value.saturation) * value.value;
    blackness = 1.0f - value.value;
}

Hwba::Hwba(const Laba& value) : Hwba(Xyza(value)) {}

Hwba::Hwba(const Lcha& value) : Hwba(Srgba(value)) {}

Hwba::Hwba(const Oklaba& value) : Hwba(LinearRgba(value)) {}

Hwba::Hwba(const Oklcha& value) : Hwba(Oklaba(value)) {}

Hwba::Hwba(const Color& value)
    : Hwba(std::visit(
          [](const auto& color) -> Hwba {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Hwba>) {
                  return color;
              } else {
                  return Hwba(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

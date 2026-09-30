#include <epix/color/hsla.hpp>
#include <algorithm>
#include <epix/color/color.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
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
const Hsla Hsla::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Hsla Hsla::WHITE(0.0f, 0.0f, 1.0f, 1.0f);

Hsla Hsla::mix(const Hsla& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Hsla(lerp_hue(hue, other.hue, factor), saturation * n_factor + other.saturation * factor,
                lightness * n_factor + other.lightness * factor, alpha * n_factor + other.alpha * factor);
}

Hsla::Hsla(const Srgba& value) : Hsla(Hsva(value)) {}

Hsla::Hsla(const Xyza& value) : Hsla(Hsva(value)) {}

Hsla::Hsla(const LinearRgba& value) : Hsla(Hsva(value)) {}

Hsla::Hsla(const Hsva& value) : Hsla(value.hue, 0.0f, 0.0f, value.alpha) {
    // Based on https://en.wikipedia.org/wiki/HSL_and_HSV#HSV_to_HSL
    lightness  = value.value * (1.0f - value.saturation / 2.0f);
    saturation = (lightness == 0.0f || lightness == 1.0f)
                     ? 0.0f
                     : (value.value - lightness) / std::min(lightness, 1.0f - lightness);
}

Hsla::Hsla(const Hwba& value) : Hsla(Hsva(value)) {}

Hsla::Hsla(const Laba& value) : Hsla(Xyza(value)) {}

Hsla::Hsla(const Lcha& value) : Hsla(Hsva(value)) {}

Hsla::Hsla(const Oklaba& value) : Hsla(LinearRgba(value)) {}

Hsla::Hsla(const Oklcha& value) : Hsla(Oklaba(value)) {}

Hsla::Hsla(const Color& value)
    : Hsla(std::visit(
          [](const auto& color) -> Hsla {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Hsla>) {
                  return color;
              } else {
                  return Hsla(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

#include <epix/color/hsva.hpp>
#include <algorithm>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
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
const Hsva Hsva::BLACK(0.0f, 0.0f, 0.0f, 1.0f);
const Hsva Hsva::WHITE(0.0f, 0.0f, 1.0f, 1.0f);

Hsva Hsva::mix(const Hsva& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Hsva(lerp_hue(hue, other.hue, factor), saturation * n_factor + other.saturation * factor,
                value * n_factor + other.value * factor, alpha * n_factor + other.alpha * factor);
}

Hsva::Hsva(const Srgba& value) : Hsva(Hwba(value)) {}

Hsva::Hsva(const Xyza& value) : Hsva(Hwba(value)) {}

Hsva::Hsva(const LinearRgba& value) : Hsva(Hwba(value)) {}

Hsva::Hsva(const Hsla& value) : Hsva(value.hue, 0.0f, 0.0f, value.alpha) {
    // Based on https://en.wikipedia.org/wiki/HSL_and_HSV#HSL_to_HSV
    const float hsv_value =
        value.lightness + value.saturation * std::min(value.lightness, 1.0f - value.lightness);
    saturation  = hsv_value == 0.0f ? 0.0f : 2.0f * (1.0f - (value.lightness / hsv_value));
    this->value = hsv_value;
}

Hsva::Hsva(const Hwba& value) : Hsva(value.hue, 0.0f, 0.0f, value.alpha) {
    // Based on https://en.wikipedia.org/wiki/HWB_color_model#Conversion
    const float hsv_value = 1.0f - value.blackness;
    saturation            = hsv_value != 0.0f ? 1.0f - (value.whiteness / hsv_value) : 0.0f;
    this->value           = hsv_value;
}

Hsva::Hsva(const Laba& value) : Hsva(Xyza(value)) {}

Hsva::Hsva(const Lcha& value) : Hsva(Hwba(value)) {}

Hsva::Hsva(const Oklaba& value) : Hsva(LinearRgba(value)) {}

Hsva::Hsva(const Oklcha& value) : Hsva(Oklaba(value)) {}

Hsva::Hsva(const Color& value)
    : Hsva(std::visit(
          [](const auto& color) -> Hsva {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Hsva>) {
                  return color;
              } else {
                  return Hsva(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

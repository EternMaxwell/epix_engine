#include <epix/color/lcha.hpp>
#include <epix/color/color.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <variant>

namespace epix::color {
const Lcha Lcha::BLACK(0.0f, 0.0f, 0.0000136603785f, 1.0f);
const Lcha Lcha::WHITE(1.0f, 0.0f, 0.0000136603785f, 1.0f);

namespace {

/** @brief Rust's `f32::to_radians`, i.e. the `DEGREES_TO_RADIANS` constant it folds from
 *  `consts::PI` (`lcha.rs:261` calls it on the hue). The f64 division narrows to the same
 *  `0.017453292f` Rust's crate uses. */
float to_radians(float degrees) { return degrees * static_cast<float>(std::numbers::pi / 180.0); }

/** @brief Rust's `f32::to_degrees`, i.e. the `RADIANS_TO_DEGREES` constant it folds from
 *  `consts::PI` (`lcha.rs:279` calls it on the computed angle). */
float to_degrees(float radians) { return radians * static_cast<float>(180.0 / std::numbers::pi); }

}  // namespace

Lcha Lcha::sequential_dispersed(std::uint32_t index) {
    // Multiply the index by the fractional part of the golden ratio to get a well-distributed
    // sequence of hues, as Bevy's `u32::wrapping_mul` does (the unsigned multiplication below
    // wraps by definition).
    constexpr std::uint32_t FRAC_U32MAX_GOLDEN_RATIO = 2654435769u;
    constexpr float RATIO_360 =
        360.0f / static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    const std::uint32_t hash = index * FRAC_U32MAX_GOLDEN_RATIO;
    const float hue          = static_cast<float>(hash) * RATIO_360;

    return Lcha::lch(0.75f, 0.35f, hue);
}

Lcha Lcha::mix(const Lcha& other, float factor) const {
    const float n_factor = 1.0f - factor;
    return Lcha(lightness * n_factor + other.lightness * factor,
                chroma * n_factor + other.chroma * factor, lerp_hue(hue, other.hue, factor),
                alpha * n_factor + other.alpha * factor);
}

Lcha::Lcha(const Laba& value) : Lcha(0.0f, 0.0f, 0.0f, value.alpha) {
    // Based on http://www.brucelindbloom.com/index.html?Eqn_Lab_to_LCH.html
    const float c = std::hypot(value.a, value.b);
    float h       = to_degrees(std::atan2(to_radians(value.b), to_radians(value.a)));
    if (h < 0.0f) {
        h += 360.0f;
    }

    lightness = value.lightness;
    chroma    = std::clamp(c, 0.0f, 1.5f);
    hue       = h;
}

Lcha::Lcha(const Srgba& value) : Lcha(Laba(value)) {}

Lcha::Lcha(const LinearRgba& value) : Lcha(Laba(value)) {}

Lcha::Lcha(const Xyza& value) : Lcha(Laba(value)) {}

Lcha::Lcha(const Hsla& value) : Lcha(Hsva(value)) {}

Lcha::Lcha(const Hsva& value) : Lcha(Hwba(value)) {}

Lcha::Lcha(const Hwba& value) : Lcha(Srgba(value)) {}

Lcha::Lcha(const Oklaba& value) : Lcha(LinearRgba(value)) {}

Lcha::Lcha(const Oklcha& value) : Lcha(Oklaba(value)) {}

Lcha::Lcha(const Color& value)
    : Lcha(std::visit(
          [](const auto& color) -> Lcha {
              using Space = std::decay_t<decltype(color)>;
              if constexpr (std::is_same_v<Space, Lcha>) {
                  return color;
              } else {
                  return Lcha(color);
              }
          },
          value.color)) {}

}  // namespace epix::color

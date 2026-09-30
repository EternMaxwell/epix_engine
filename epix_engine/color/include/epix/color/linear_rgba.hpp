#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <epix/color/color_ops.hpp>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::color {

/** @brief Linear RGB color with alpha (Bevy `LinearRgba`,
 *  `bevy_color/src/linear_rgba.rs:27-36`).
 *
 *  Fields keep Bevy's names and `[0.0, 1.0]` meaning. Rust's derived `Default` is overridden to
 *  return `WHITE` (`linear_rgba.rs:166-171`), which C++ encodes as the member initialisers below.
 *  `impl_componentwise_vector_space!` (`linear_rgba.rs:40`) is expanded by
 *  `EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE`; the `color_ops.rs` traits are the members documented
 *  on that macro, and Rust's `PartialEq` derive is the defaulted `operator==`.
 *
 *  Every `impl From<A> for LinearRgba` is this type's converting constructor from `A`: Bevy spreads
 *  those blocks over the file of the space converted from, while the port keeps every conversion
 *  that *produces* a `LinearRgba` here, so a reader asking "how do I get a linear colour" has one
 *  place to look. The delegation order is Bevy's own: the hue-based spaces hop through `Hwba`,
 *  `Laba` through `Xyza`, `Lcha` through `Laba`, and `Oklcha` through `Oklaba`. */
EPIX_EXPORT struct EPIX_API LinearRgba {
    /** @brief The red channel. [0.0, 1.0] */
    float red = 1.0f;
    /** @brief The green channel. [0.0, 1.0] */
    float green = 1.0f;
    /** @brief The blue channel. [0.0, 1.0] */
    float blue = 1.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const LinearRgba&) const = default;

    constexpr LinearRgba() = default;

    /** @brief Construct a color from components (Bevy `LinearRgba::new`,
     *  `linear_rgba.rs:104-111`). */
    constexpr LinearRgba(float red, float green, float blue, float alpha)
        : red(red), green(green), blue(blue), alpha(alpha) {}

    /** @brief Bevy `impl From<Srgba> for LinearRgba` (`srgba.rs:404-414`): the sRGB transfer
     *  function on every channel, alpha copied. */
    LinearRgba(const Srgba& value);

    /** @brief Bevy `impl From<Xyza> for LinearRgba` (`xyza.rs:235-245`): the XYZ → linear sRGB
     *  matrix. */
    LinearRgba(const Xyza& value);

    /** @brief Bevy `impl From<Hsla> for LinearRgba` (`hsla.rs:339-343`): hops through `Hsva`. */
    LinearRgba(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for LinearRgba` (`hsva.rs:269-273`): hops through `Hwba`. */
    LinearRgba(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for LinearRgba` (`hwba.rs:277-281`): hops through `Srgba`. */
    LinearRgba(const Hwba& value);

    /** @brief Bevy `impl From<Laba> for LinearRgba` (`laba.rs:311-315`): hops through `Xyza`. */
    LinearRgba(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for LinearRgba` (`lcha.rs:320-324`): hops through `Laba`. */
    LinearRgba(const Lcha& value);

    /** @brief Bevy `impl From<Oklaba> for LinearRgba` (`oklaba.rs:246-278`): the Oklab → linear
     *  sRGB matrix, with the intermediate LMS values cubed. */
    LinearRgba(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for LinearRgba` (`oklcha.rs:361-365`): hops through
     *  `Oklaba`. */
    LinearRgba(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for LinearRgba` (`color.rs:548-564`): the variant's own value
     *  is returned unchanged, every other variant converts. */
    LinearRgba(const Color& value);

    /** @brief Build a color from an RGBA array (Bevy `LinearRgba::from_f32_array`,
     *  `linear_rgba.rs:269-276`). */
    LinearRgba(const std::array<float, 4>& color)
        : red(color[0]), green(color[1]), blue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an RGB array, with full alpha (Bevy
     *  `LinearRgba::from_f32_array_no_alpha`, `linear_rgba.rs:278-285`). */
    LinearRgba(const std::array<float, 3>& color)
        : red(color[0]), green(color[1]), blue(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `LinearRgba::from_vec4`,
     *  `linear_rgba.rs:287-294`). */
    LinearRgba(const glm::vec4& color)
        : red(color[0]), green(color[1]), blue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy `LinearRgba::from_vec3`,
     *  `linear_rgba.rs:296-303`). */
    LinearRgba(const glm::vec3& color) : red(color[0]), green(color[1]), blue(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a packed `u8` RGBA array (Bevy `LinearRgba::from_u8_array`,
     *  `linear_rgba.rs:316-318`). */
    LinearRgba(const std::array<std::uint8_t, 4>& color);

    /** @brief Build a color from a packed `u8` RGB array, with full alpha (Bevy
     *  `LinearRgba::from_u8_array_no_alpha`, `linear_rgba.rs:320-322`). */
    LinearRgba(const std::array<std::uint8_t, 3>& color);

    EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE(LinearRgba, red, green, blue, alpha)

    /** @brief A fully black color with full alpha (Bevy `LinearRgba::BLACK`). */
    static const LinearRgba BLACK;

    /** @brief A fully white color with full alpha (Bevy `LinearRgba::WHITE`). */
    static const LinearRgba WHITE;

    /** @brief A fully transparent color (Bevy `LinearRgba::NONE`). */
    static const LinearRgba NONE;

    /** @brief A fully red color with full alpha (Bevy `LinearRgba::RED`). */
    static const LinearRgba RED;

    /** @brief A fully green color with full alpha (Bevy `LinearRgba::GREEN`). */
    static const LinearRgba GREEN;

    /** @brief A fully blue color with full alpha (Bevy `LinearRgba::BLUE`). */
    static const LinearRgba BLUE;

    /** @brief An invalid color (Bevy `LinearRgba::NAN`, `linear_rgba.rs:91-101`). In some
     *  rendering applications the color is ignored, which enables hiding lines by setting their
     *  color to `NAN`. The member is spelled `NaN` because `<cmath>` defines an object-like macro
     *  named `NAN` that would rewrite this declaration into `LinearRgba::__builtin_nanf("")`;
     *  this is the one name in the port's colour API that a C++ keyword or macro forces to
     *  differ from Bevy's. */
    static const LinearRgba NaN;

    /** @brief Construct a color from (red, green, blue), with the default alpha (1.0) (Bevy
     *  `LinearRgba::rgb`, `linear_rgba.rs:120-127`). */
    constexpr static LinearRgba rgb(float red, float green, float blue) {
        return LinearRgba(red, green, blue, 1.0f);
    }

    /** @brief Return a copy of this color with the red channel set to the given value (Bevy
     *  `LinearRgba::with_red`, `linear_rgba.rs:130-132`). */
    constexpr LinearRgba with_red(float red) const {
        LinearRgba result = *this;
        result.red        = red;
        return result;
    }

    /** @brief Return a copy of this color with the green channel set to the given value (Bevy
     *  `LinearRgba::with_green`, `linear_rgba.rs:135-137`). */
    constexpr LinearRgba with_green(float green) const {
        LinearRgba result = *this;
        result.green      = green;
        return result;
    }

    /** @brief Return a copy of this color with the blue channel set to the given value (Bevy
     *  `LinearRgba::with_blue`, `linear_rgba.rs:140-142`). */
    constexpr LinearRgba with_blue(float blue) const {
        LinearRgba result = *this;
        result.blue       = blue;
        return result;
    }

    /** @brief Converts this color to a `u32` (Bevy `LinearRgba::as_u32`, `linear_rgba.rs:161-163`).
     *  Maps the RGBA channels in RGBA order to a little-endian byte array, so `A` is the most
     *  significant byte and `R` the least significant. */
    std::uint32_t as_u32() const;

    /** @brief Luminance calculated using the
     *  [CIE XYZ formula](https://en.wikipedia.org/wiki/Relative_luminance) (Bevy
     *  `Luminance for LinearRgba`, `linear_rgba.rs:173-178`). Note that this is not the same number
     *  as `Xyza(*this).y`: Bevy rounds the coefficients here and keeps full precision there. */
    float luminance() const;

    /** @brief A copy of this color with the luminance set to the given value, channels scaled and
     *  clamped, alpha unchanged (Bevy `LinearRgba::with_luminance`, `linear_rgba.rs:180-190`). */
    LinearRgba with_luminance(float luminance) const;

    /** @brief Make the color darker by some amount (Bevy `LinearRgba::darker`,
     *  `linear_rgba.rs:192-197`). */
    LinearRgba darker(float amount) const;

    /** @brief Make the color lighter by some amount (Bevy `LinearRgba::lighter`,
     *  `linear_rgba.rs:199-204`). */
    LinearRgba lighter(float amount) const;

    /** @brief Linearly interpolate between this color and another (Bevy `Mix for LinearRgba`,
     *  `linear_rgba.rs:207-218`). */
    LinearRgba mix(const LinearRgba& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const LinearRgba& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static LinearRgba gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`). */
    constexpr LinearRgba with_alpha(float alpha) const {
        LinearRgba result = *this;
        result.alpha      = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, spelled `get_alpha` because the field keeps
     *  that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief The distance between this and another color as if they were coordinates in a
     *  Euclidean space; alpha is not considered (Bevy `EuclideanDistance for LinearRgba`,
     *  `linear_rgba.rs:242-250`). */
    float distance_squared(const LinearRgba& other) const;

    /** @brief The provided `EuclideanDistance::distance` (`color_difference.rs:7-11`). */
    float distance(const LinearRgba& other) const { return std::sqrt(distance_squared(other)); }

    /** @brief The components as an RGBA array (Bevy `ColorToComponents::to_f32_array`,
     *  `linear_rgba.rs:252-255`; the port drops the `to_` prefix, so the constructor above and
     *  this accessor are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {red, green, blue, alpha}; }

    /** @brief The components as an RGB array (Bevy `ColorToComponents::to_f32_array_no_alpha`,
     *  `linear_rgba.rs:257-259`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {red, green, blue}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `linear_rgba.rs:261-263`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(red, green, blue, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `linear_rgba.rs:265-267`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(red, green, blue); }

    /** @brief The packed `u8` RGBA representation (Bevy `ColorToPacked::to_u8_array`,
     *  `linear_rgba.rs:306-310`). */
    std::array<std::uint8_t, 4> u8_array() const;

    /** @brief The packed `u8` RGB representation (Bevy `ColorToPacked::to_u8_array_no_alpha`,
     *  `linear_rgba.rs:312-314`). */
    std::array<std::uint8_t, 3> u8_array_no_alpha() const;

private:
    /** @brief Make the color lighter or darker by some amount (Bevy `LinearRgba::adjust_lightness`,
     *  `linear_rgba.rs:144-155`); the mutating half `darker` and `lighter` share. */
    void adjust_lightness(float amount);
};

}  // namespace epix::color

#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/color/color_ops.hpp>

namespace epix::color {

/** @brief Error returned if a hex string could not be parsed as a color (Bevy `HexColorError`,
 *  `bevy_color/src/srgba.rs:430-442`).
 *
 *  Bevy's `Parse(ParseIntError)` variant carries `core::num::ParseIntError`, which has no C++
 *  counterpart, so the port keeps the variant as a payload-free `Kind::Parse`. The invalid
 *  character case - which Bevy reports as `Parse`, because `from_str_radix` is what finds it - is
 *  reported as `Kind::Char` together with the offending character, since that is the only payload
 *  the port can carry. Each side has one variant the parser never constructs: Bevy's `Char` and
 *  the port's `Parse`.
 *
 *  The three `#[error(..)]` messages have no C++ counterpart either (there is no `Display`), and
 *  the type is declared here rather than after the `impl` blocks it belongs to in Bevy, because
 *  `std::expected<Srgba, HexColorError>` needs the error type complete. */
EPIX_EXPORT struct EPIX_API HexColorError {
    /** @brief Which of Bevy's three variants this is (`srgba.rs:432-441`). */
    enum class Kind { Parse, Length, Char };

    /** @brief The failing variant. */
    Kind kind;

    /** @brief The offending character of `Kind::Char`; `'\0'` for every other kind. */
    char value = '\0';
};

/** @brief Non-linear standard RGB with alpha (Bevy `Srgba`, `bevy_color/src/srgba.rs:12-37`).
 *
 *  Fields keep Bevy's names and `[0.0, 1.0]` meaning. Rust's derived `Default` is overridden to
 *  return `WHITE` (`srgba.rs:240-244`), which C++ encodes as the member initialisers below.
 *  `impl_componentwise_vector_space!` (`srgba.rs:41`) is expanded by
 *  `EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE`; the `color_ops.rs` traits are the members documented
 *  on that macro, and Rust's `PartialEq` derive is the defaulted `operator==`.
 *
 *  Not ported: `#[derive(Reflect)]`/`#[reflect(..)]`, serde, `Debug`, and the crate-internal
 *  `StandardColor` marker trait (`srgba.rs:39`). `HexColorError`'s `ParseIntError` payload is the
 *  one divergence inside the type's own API; see its doc comment. */
EPIX_EXPORT struct EPIX_API Srgba {
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
    constexpr bool operator==(const Srgba&) const = default;

    constexpr Srgba() = default;

    /** @brief Construct a color from components (Bevy `Srgba::new`, `srgba.rs:79-94`).
     *
     *  @param red Red channel. [0.0, 1.0]
     *  @param green Green channel. [0.0, 1.0]
     *  @param blue Blue channel. [0.0, 1.0]
     *  @param alpha Alpha channel. [0.0, 1.0] */
    constexpr Srgba(float red, float green, float blue, float alpha)
        : red(red), green(green), blue(blue), alpha(alpha) {}

    EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE(Srgba, red, green, blue, alpha)

    /** @brief The standard black color with full alpha (Bevy `Srgba::BLACK`, `srgba.rs:47-48`). */
    static const Srgba BLACK;

    /** @brief A fully transparent color (Bevy `Srgba::NONE`, `srgba.rs:49-51`), which Bevy also
     *  aliases as `transparent`. */
    static const Srgba NONE;

    /** @brief The standard white color with full alpha (Bevy `Srgba::WHITE`, `srgba.rs:52-53`). */
    static const Srgba WHITE;

    /** @brief A fully red color with full alpha (Bevy `Srgba::RED`, `srgba.rs:55-61`). */
    static const Srgba RED;

    /** @brief A fully green color with full alpha (Bevy `Srgba::GREEN`, `srgba.rs:63-69`). */
    static const Srgba GREEN;

    /** @brief A fully blue color with full alpha (Bevy `Srgba::BLUE`, `srgba.rs:71-77`). */
    static const Srgba BLUE;

    /** @brief Construct a color from (red, green, blue), with the default alpha (1.0) (Bevy
     *  `Srgba::rgb`, `srgba.rs:96-110`). */
    constexpr static Srgba rgb(float red, float green, float blue) { return Srgba(red, green, blue, 1.0f); }

    /** @brief Return a copy of this color with the red channel set to the given value (Bevy
     *  `Srgba::with_red`, `srgba.rs:112-115`). */
    constexpr Srgba with_red(float red) const {
        Srgba result = *this;
        result.red   = red;
        return result;
    }

    /** @brief Return a copy of this color with the green channel set to the given value (Bevy
     *  `Srgba::with_green`, `srgba.rs:117-120`). */
    constexpr Srgba with_green(float green) const {
        Srgba result = *this;
        result.green = green;
        return result;
    }

    /** @brief Return a copy of this color with the blue channel set to the given value (Bevy
     *  `Srgba::with_blue`, `srgba.rs:122-125`). */
    constexpr Srgba with_blue(float blue) const {
        Srgba result = *this;
        result.blue  = blue;
        return result;
    }

    /** @brief New `Srgba` from a CSS-style hexadecimal string (Bevy `Srgba::hex`,
     *  `srgba.rs:127-173`). A leading `#` is stripped; 3, 4, 6 and 8 digits are accepted, the
     *  3- and 4-digit short forms replicating each nibble (`"f2a"` -> `rgb_u8(255, 34, 170)`).
     *  Bevy is generic over `T: AsRef<str>` and returns `Result<Self, HexColorError>`; the port
     *  takes a `std::string_view` and returns `std::expected`. */
    static std::expected<Srgba, HexColorError> hex(std::string_view hex);

    /** @brief Convert this color to CSS-style hexadecimal notation (Bevy `Srgba::to_hex`,
     *  `srgba.rs:175-183`; the port drops the `to_` prefix, so this accessor and the static parser
     *  above are the two halves of Bevy's `hex` name): `#RRGGBB`, or `#RRGGBBAA` when the packed
     *  alpha is not 255. Bevy gates this on its `alloc` feature and returns `String`; the port
     *  returns `std::string`. Overloading Bevy's static parser with this non-static accessor is
     *  legal C++ because the parameter lists differ. */
    std::string hex() const;

    /** @brief New `Srgba` from sRGB colorspace (Bevy `Srgba::rgb_u8`, `srgba.rs:185-196`).
     *
     *  @param r Red channel. [0, 255]
     *  @param g Green channel. [0, 255]
     *  @param b Blue channel. [0, 255] */
    static Srgba rgb_u8(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return Srgba(std::array<std::uint8_t, 3>{r, g, b});
    }

    /** @brief New `Srgba` from sRGB colorspace (Bevy `Srgba::rgba_u8`, `srgba.rs:198-212`).
     *
     *  @param r Red channel. [0, 255]
     *  @param g Green channel. [0, 255]
     *  @param b Blue channel. [0, 255]
     *  @param a Alpha channel. [0, 255] */
    static Srgba rgba_u8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        return Srgba(std::array<std::uint8_t, 4>{r, g, b, a});
    }

    /** @brief Converts a non-linear sRGB value to a linear one via
     *  [gamma correction](https://en.wikipedia.org/wiki/Gamma_correction) (Bevy
     *  `Srgba::gamma_function`, `srgba.rs:214-224`). Negative values are returned unchanged; below
     *  `0.04045` the transfer function is the linear falloff `value / 12.92`, above it the
     *  `pow((value + 0.055) / 1.055, 2.4)` gamma curve. */
    static float gamma_function(float value);

    /** @brief Converts a linear sRGB value to a non-linear one via
     *  [gamma correction](https://en.wikipedia.org/wiki/Gamma_correction) (Bevy
     *  `Srgba::gamma_function_inverse`, `srgba.rs:226-237`). Negative values are returned
     *  unchanged; below `0.0031308` the linear falloff `value * 12.92` applies, above it
     *  `1.055 * pow(value, 1.0 / 2.4) - 0.055`. */
    static float gamma_function_inverse(float value);

    /** @brief Luminance of the equivalent linear color (Bevy `Luminance for Srgba`,
     *  `srgba.rs:246-251`). */
    float luminance() const;

    /** @brief A copy of this color with the luminance set to the given value, going through
     *  `gamma_function` on the way in (Bevy `Srgba::with_luminance`, `srgba.rs:253-259`). */
    Srgba with_luminance(float luminance) const;

    /** @brief Make the color darker by some amount (Bevy `Srgba::darker`, `srgba.rs:261-266`). */
    Srgba darker(float amount) const;

    /** @brief Make the color lighter by some amount (Bevy `Srgba::lighter`, `srgba.rs:267-271`). */
    Srgba lighter(float amount) const;

    /** @brief Linearly interpolate between this color and another, in non-linear sRGB space and
     *  on every channel including alpha (Bevy `Mix for Srgba`, `srgba.rs:274-285`). */
    Srgba mix(const Srgba& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Srgba& other, float factor) { *this = mix(other, factor); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`). */
    constexpr Srgba with_alpha(float alpha) const {
        Srgba result = *this;
        result.alpha = alpha;
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
     *  Euclidean space; alpha is not considered (Bevy `EuclideanDistance for Srgba`,
     *  `srgba.rs:304-312`). */
    float distance_squared(const Srgba& other) const;

    /** @brief The provided `EuclideanDistance::distance` (`color_difference.rs:7-11`). */
    float distance(const Srgba& other) const { return std::sqrt(distance_squared(other)); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants (`srgba.rs:314-317`). */
    static Srgba gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief The components as an RGBA array (Bevy `ColorToComponents::to_f32_array`,
     *  `srgba.rs:319-322`; the port drops the `to_` prefix, so this accessor and the matching
     *  constructor below are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {red, green, blue, alpha}; }

    /** @brief The components as an RGB array (Bevy `ColorToComponents::to_f32_array_no_alpha`,
     *  `srgba.rs:324-326`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {red, green, blue}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `srgba.rs:328-330`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(red, green, blue, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `srgba.rs:332-334`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(red, green, blue); }

    /** @brief Build a color from an RGBA array (Bevy `ColorToComponents::from_f32_array`,
     *  `srgba.rs:336-343`). */
    constexpr Srgba(const std::array<float, 4>& color)
        : red(color[0]), green(color[1]), blue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an RGB array, with full alpha (Bevy
     *  `ColorToComponents::from_f32_array_no_alpha`, `srgba.rs:345-352`). */
    constexpr Srgba(const std::array<float, 3>& color)
        : red(color[0]), green(color[1]), blue(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `ColorToComponents::from_vec4`,
     *  `srgba.rs:354-361`). */
    constexpr Srgba(const glm::vec4& color)
        : red(color[0]), green(color[1]), blue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy
     *  `ColorToComponents::from_vec3`, `srgba.rs:363-370`). */
    constexpr Srgba(const glm::vec3& color) : red(color[0]), green(color[1]), blue(color[2]), alpha(1.0f) {}

    /** @brief The packed `u8` RGBA representation (Bevy `ColorToPacked::to_u8_array`,
     *  `srgba.rs:373-377`). Every channel is clamped to `[0.0, 1.0]` and rounded. */
    std::array<std::uint8_t, 4> u8_array() const;

    /** @brief The packed `u8` RGB representation (Bevy `ColorToPacked::to_u8_array_no_alpha`,
     *  `srgba.rs:379-381`). */
    std::array<std::uint8_t, 3> u8_array_no_alpha() const;

    /** @brief Build a color from a packed `u8` RGBA array (Bevy `ColorToPacked::from_u8_array`,
     *  `srgba.rs:383-385`). */
    Srgba(const std::array<std::uint8_t, 4>& color);

    /** @brief Build a color from a packed `u8` RGB array, with full alpha (Bevy
     *  `ColorToPacked::from_u8_array_no_alpha`, `srgba.rs:387-389`). */
    Srgba(const std::array<std::uint8_t, 3>& color);

    // Every `impl From<A> for Srgba` is this type's converting constructor from `A`. Bevy spreads
    // the ten blocks over the file of the type being converted from; the port defines every
    // conversion that *produces* an `Srgba` here, so a reader asking "how do I get an sRGB colour"
    // has one place to look. The delegation order is Bevy's own: the hue-based spaces hop through
    // `Hwba`, `Xyza` through `LinearRgba`, `Lcha` through `Laba`, and `Oklcha` through `Oklaba`.

    /** @brief Bevy `impl From<LinearRgba> for Srgba` (`srgba.rs:392-402`): the sRGB transfer
     *  function's inverse on every channel, alpha copied. */
    Srgba(const LinearRgba& value);

    /** @brief Bevy `impl From<Xyza> for Srgba` (`srgba.rs:418-422`): hops through
     *  `LinearRgba`. */
    Srgba(const Xyza& value);

    /** @brief Bevy `impl From<Hsla> for Srgba` (`hsla.rs:327-331`): hops through `Hsva`. */
    Srgba(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Srgba` (`hsva.rs:257-261`): hops through `Hwba`. */
    Srgba(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Srgba` (`hwba.rs:232-277`): the HWB to RGB sector
     *  formula, Appendix B of "HWB - A More Intuitive Hue-Based Color Model". */
    Srgba(const Hwba& value);

    /** @brief Bevy `impl From<Laba> for Srgba` (`laba.rs:299-303`): hops through `Xyza`. */
    Srgba(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for Srgba` (`lcha.rs:308-312`): hops through `Laba`. */
    Srgba(const Lcha& value);

    /** @brief Bevy `impl From<Oklaba> for Srgba` (`oklaba.rs:334-338`): hops through
     *  `LinearRgba`. */
    Srgba(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Srgba` (`oklcha.rs:373-377`): hops through
     *  `Oklaba`. */
    Srgba(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Srgba` (`color.rs:531-547`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Srgba(const Color& value);
};

}  // namespace epix::color

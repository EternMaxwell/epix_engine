#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <variant>
#include <epix/common.hpp>
#endif
#include <epix/color/color_ops.hpp>
#include <epix/color/hsla.hpp>
#include <epix/color/hsva.hpp>
#include <epix/color/hwba.hpp>
#include <epix/color/laba.hpp>
#include <epix/color/lcha.hpp>
#include <epix/color/linear_rgba.hpp>
#include <epix/color/oklaba.hpp>
#include <epix/color/oklcha.hpp>
#include <epix/color/srgba.hpp>
#include <epix/color/xyza.hpp>

namespace epix::color {

/** @brief Error signalling that two colors are in different color spaces, so that an operation
 *  which requires a single space cannot be performed (Bevy `MismatchedUnitsError`,
 *  `bevy_math/src/common_traits.rs:543-546`).
 *
 *  Bevy's type is a payload-free unit struct whose only derives are `Debug` and `Error`; the port
 *  keeps it payload-free, so a caller can only test whether the operation failed. */
EPIX_EXPORT struct EPIX_API MismatchedUnitsError {};

/** @brief An enumerated type that can represent any of the color types in this crate (Bevy `Color`,
 *  `bevy_color/src/color.rs:56-77`).
 *
 *  Bevy's `Color` is an enum with one variant per color space; the port spells it as the
 *  `std::variant` member `color` (the shape `camera::Projection` already uses for its own variant
 *  wrapper) with one implicit converting constructor per variant, which is what Bevy's
 *  `derive_more::From` (`color.rs:45`) generates.
 *
 *  Every `impl From<A> for B` in the crate is `B`'s converting constructor `B(const A&)`; the
 *  conversion *out of* this type is therefore spelled `X(*this)` (`color.rs:531-699`, owned by the
 *  space that produces the result) and read back through `linear()` / `srgba()` below. As
 *  everywhere in the port, an accessor whose Bevy name carries a `to_` prefix drops it.
 *
 *  The variant itself owns no color-space maths:
 *  every operation delegates to the contained space's own members, convert the operands when
 *  needed.
 *
 *  Bevy's docs (`color.rs:21-26`) name `Oklch` as the intermediary for operations a space does not
 *  implement: this is `ChosenColorSpace` below, and, as in Bevy, the result is converted back into
 *  the variant's original space.
 *
 *  Not ported: `#[derive(Reflect)]`/`#[reflect(..)]`, serde, `Debug`, and the crate-internal
 *  `StandardColor` marker trait (`color.rs:79`). Rust's derived `PartialEq` is the defaulted
 *  `operator==`. */
EPIX_EXPORT struct EPIX_API Color {
    /** @brief The color space this color is currently stored in (Bevy's ten `Color` variants,
     *  `color.rs:56-77`).
     *
     *  Rust's derived `Default` is overridden to return `Color::WHITE` (`color.rs:473-478`), a
     *  `LinearRgba`, so the member defaults to `LinearRgba::WHITE`. */
    std::variant<Srgba, LinearRgba, Hsla, Hsva, Hwba, Laba, Lcha, Oklaba, Oklcha, Xyza> color =
        LinearRgba::WHITE;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq, From`; the port leaves copying
     *  implicit, spells the equality, and turns the `From` derives into the constructors below. */
    constexpr bool operator==(const Color&) const = default;

    /** @brief The default color: a fully white `LinearRgba` with full alpha (Bevy `impl Default for
     *  Color`, `color.rs:473-478`).
     *
     *  Not `constexpr`: the default member initializer of `color` reads `LinearRgba::WHITE`, an
     *  out-of-line constant that cannot be `constexpr` (a class cannot hold an in-class `constexpr`
     *  member of its own still-incomplete type). */
    Color() = default;

    // Bevy's ten `impl From<Color> for X` blocks (`color.rs:531-699`) are owned by the space they
    // produce, exactly like every other conversion in the crate: each one is that space's
    // converting constructor `X(const Color&)`, declared in `X`'s header and defined in `X`'s
    // `.cpp`, so a call site reads `X(color)` where Bevy reads `X::from(color)`. This type
    // therefore only declares the opposite direction: Bevy's `derive_more::From` (`color.rs:45`),
    // which gives `Color` one implicit constructor per variant.

    /** @brief Store a non-linear standard RGB color (Bevy `From<Srgba> for Color`,
     *  `color.rs:45`). */
    constexpr Color(const Srgba& value) : color(value) {}

    /** @brief Store a linear standard RGB color (Bevy `From<LinearRgba> for Color`,
     *  `color.rs:45`). */
    constexpr Color(const LinearRgba& value) : color(value) {}

    /** @brief Store an HSL color (Bevy `From<Hsla> for Color`, `color.rs:45`). */
    constexpr Color(const Hsla& value) : color(value) {}

    /** @brief Store an HSV color (Bevy `From<Hsva> for Color`, `color.rs:45`). */
    constexpr Color(const Hsva& value) : color(value) {}

    /** @brief Store an HWB color (Bevy `From<Hwba> for Color`, `color.rs:45`). */
    constexpr Color(const Hwba& value) : color(value) {}

    /** @brief Store a LAB color (Bevy `From<Laba> for Color`, `color.rs:45`). */
    constexpr Color(const Laba& value) : color(value) {}

    /** @brief Store an LCH color (Bevy `From<Lcha> for Color`, `color.rs:45`). */
    constexpr Color(const Lcha& value) : color(value) {}

    /** @brief Store an Oklab color (Bevy `From<Oklaba> for Color`, `color.rs:45`). */
    constexpr Color(const Oklaba& value) : color(value) {}

    /** @brief Store an Oklch color (Bevy `From<Oklcha> for Color`, `color.rs:45`). */
    constexpr Color(const Oklcha& value) : color(value) {}

    /** @brief Store an XYZ color (Bevy `From<Xyza> for Color`, `color.rs:45`). */
    constexpr Color(const Xyza& value) : color(value) {}

    /** @brief Creates a new `Color` storing an `Srgba` color (Bevy `Color::srgba`,
     *  `color.rs:100-107`).
     *
     *  @param red Red channel. [0.0, 1.0]
     *  @param green Green channel. [0.0, 1.0]
     *  @param blue Blue channel. [0.0, 1.0]
     *  @param alpha Alpha channel. [0.0, 1.0] */
    constexpr static Color srgba(float red, float green, float blue, float alpha) {
        return Color(Srgba{red, green, blue, alpha});
    }

    /** @brief Creates a new `Color` storing an `Srgba` color with an alpha of 1.0 (Bevy
     *  `Color::srgb`, `color.rs:116-123`). */
    constexpr static Color srgb(float red, float green, float blue) {
        return Color(Srgba{red, green, blue, 1.0f});
    }

    /** @brief Reads an array of floats to creates a new `Color` storing an `Srgba` color with an
     *  alpha of 1.0 (Bevy `Color::srgb_from_array`, `color.rs:129-136`). */
    constexpr static Color srgb_from_array(const std::array<float, 3>& array) {
        return Color(Srgba{array[0], array[1], array[2], 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Srgba` color from `u8` values (Bevy
     *  `Color::srgba_u8`, `color.rs:146-153`). Bevy's `u8 as f32 / 255.0` is the port's
     *  `static_cast<float>` division. */
    constexpr static Color srgba_u8(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                                    std::uint8_t alpha) {
        return Color(Srgba{static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f,
                           static_cast<float>(blue) / 255.0f, static_cast<float>(alpha) / 255.0f});
    }

    /** @brief Creates a new `Color` storing an `Srgba` color from `u8` values with an alpha of 1.0
     *  (Bevy `Color::srgb_u8`, `color.rs:162-169`). */
    constexpr static Color srgb_u8(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        return Color(Srgba{static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f,
                           static_cast<float>(blue) / 255.0f, 1.0f});
    }

    /** @brief Creates a new `Color` storing a `LinearRgba` color (Bevy `Color::linear_rgba`,
     *  `color.rs:179-186`). */
    constexpr static Color linear_rgba(float red, float green, float blue, float alpha) {
        return Color(LinearRgba{red, green, blue, alpha});
    }

    /** @brief Creates a new `Color` storing a `LinearRgba` color with an alpha of 1.0 (Bevy
     *  `Color::linear_rgb`, `color.rs:195-202`). */
    constexpr static Color linear_rgb(float red, float green, float blue) {
        return Color(LinearRgba{red, green, blue, 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Hsla` color (Bevy `Color::hsla`,
     *  `color.rs:212-219`). */
    constexpr static Color hsla(float hue, float saturation, float lightness, float alpha) {
        return Color(Hsla{hue, saturation, lightness, alpha});
    }

    /** @brief Creates a new `Color` storing an `Hsla` color with an alpha of 1.0 (Bevy
     *  `Color::hsl`, `color.rs:228-235`). */
    constexpr static Color hsl(float hue, float saturation, float lightness) {
        return Color(Hsla{hue, saturation, lightness, 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Hsva` color (Bevy `Color::hsva`,
     *  `color.rs:245-252`). */
    constexpr static Color hsva(float hue, float saturation, float value, float alpha) {
        return Color(Hsva{hue, saturation, value, alpha});
    }

    /** @brief Creates a new `Color` storing an `Hsva` color with an alpha of 1.0 (Bevy
     *  `Color::hsv`, `color.rs:261-268`). */
    constexpr static Color hsv(float hue, float saturation, float value) {
        return Color(Hsva{hue, saturation, value, 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Hwba` color (Bevy `Color::hwba`,
     *  `color.rs:278-285`). */
    constexpr static Color hwba(float hue, float whiteness, float blackness, float alpha) {
        return Color(Hwba{hue, whiteness, blackness, alpha});
    }

    /** @brief Creates a new `Color` storing an `Hwba` color with an alpha of 1.0 (Bevy
     *  `Color::hwb`, `color.rs:294-301`). */
    constexpr static Color hwb(float hue, float whiteness, float blackness) {
        return Color(Hwba{hue, whiteness, blackness, 1.0f});
    }

    /** @brief Creates a new `Color` storing a `Laba` color (Bevy `Color::laba`,
     *  `color.rs:311-318`). */
    constexpr static Color laba(float lightness, float a, float b, float alpha) {
        return Color(Laba{lightness, a, b, alpha});
    }

    /** @brief Creates a new `Color` storing a `Laba` color with an alpha of 1.0 (Bevy
     *  `Color::lab`, `color.rs:327-334`). */
    constexpr static Color lab(float lightness, float a, float b) {
        return Color(Laba{lightness, a, b, 1.0f});
    }

    /** @brief Creates a new `Color` storing a `Lcha` color (Bevy `Color::lcha`,
     *  `color.rs:344-351`). */
    constexpr static Color lcha(float lightness, float chroma, float hue, float alpha) {
        return Color(Lcha{lightness, chroma, hue, alpha});
    }

    /** @brief Creates a new `Color` storing a `Lcha` color with an alpha of 1.0 (Bevy
     *  `Color::lch`, `color.rs:360-367`). */
    constexpr static Color lch(float lightness, float chroma, float hue) {
        return Color(Lcha{lightness, chroma, hue, 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Oklaba` color (Bevy `Color::oklaba`,
     *  `color.rs:377-384`). */
    constexpr static Color oklaba(float lightness, float a, float b, float alpha) {
        return Color(Oklaba{lightness, a, b, alpha});
    }

    /** @brief Creates a new `Color` storing an `Oklaba` color with an alpha of 1.0 (Bevy
     *  `Color::oklab`, `color.rs:393-400`). */
    constexpr static Color oklab(float lightness, float a, float b) {
        return Color(Oklaba{lightness, a, b, 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Oklcha` color (Bevy `Color::oklcha`,
     *  `color.rs:410-417`). */
    constexpr static Color oklcha(float lightness, float chroma, float hue, float alpha) {
        return Color(Oklcha{lightness, chroma, hue, alpha});
    }

    /** @brief Creates a new `Color` storing an `Oklcha` color with an alpha of 1.0 (Bevy
     *  `Color::oklch`, `color.rs:426-433`). */
    constexpr static Color oklch(float lightness, float chroma, float hue) {
        return Color(Oklcha{lightness, chroma, hue, 1.0f});
    }

    /** @brief Creates a new `Color` storing an `Xyza` color (Bevy `Color::xyza`,
     *  `color.rs:443-445`). */
    constexpr static Color xyza(float x, float y, float z, float alpha) {
        return Color(Xyza{x, y, z, alpha});
    }

    /** @brief Creates a new `Color` storing an `Xyza` color with an alpha of 1.0 (Bevy
     *  `Color::xyz`, `color.rs:454-461`). */
    constexpr static Color xyz(float x, float y, float z) { return Color(Xyza{x, y, z, 1.0f}); }

    /** @brief A fully white `Color::LinearRgba` color with an alpha of 1.0 (Bevy `Color::WHITE`,
     *  `color.rs:463-464`). */
    static const Color WHITE;

    /** @brief A fully black `Color::LinearRgba` color with an alpha of 1.0 (Bevy `Color::BLACK`,
     *  `color.rs:466-467`). */
    static const Color BLACK;

    /** @brief A fully transparent `Color::LinearRgba` color with 0 red, green and blue (Bevy
     *  `Color::NONE`, `color.rs:469-470`). */
    static const Color NONE;

    /** @brief Return the color as a linear RGBA color (Bevy `Color::to_linear`,
     *  `color.rs:82-85`): Bevy's `(*self).into()` is this crate's converting constructor, so the
     *  port reads `LinearRgba(*this)`; the accessor drops Bevy's `to_` prefix like every other
     *  accessor. */
    LinearRgba linear() const { return LinearRgba(*this); }

    /** @brief Return the color as an SRGBA color (Bevy `Color::to_srgba`, `color.rs:87-90`):
     *  Bevy's `(*self).into()` is `Srgba(*this)`. This overloads with the `srgba(red, green, blue,
     *  alpha)` factory above, which is legal because the parameter lists differ. */
    Srgba srgba() const { return Srgba(*this); }

    /** @brief Return a new version of this color with the given alpha value; the stored color space
     *  is preserved (Bevy `Alpha::with_alpha for Color`, `color.rs:481-498`). */
    Color with_alpha(float alpha) const;

    /** @brief Return the alpha component of this color (Bevy `Alpha::alpha for Color`,
     *  `color.rs:500-513`). */
    float alpha() const;

    /** @brief Sets the alpha component of this color (Bevy `Alpha::set_alpha for Color`,
     *  `color.rs:515-529`). */
    void set_alpha(float alpha) { *this = with_alpha(alpha); }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    bool is_fully_transparent() const { return alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    bool is_fully_opaque() const { return alpha() >= 1.0f; }

    /** @brief The color space Bevy falls back to for operations that a space does not implement,
     *  whose result is converted back into the original space (Bevy `type ChosenColorSpace =
     *  Oklcha`, `color.rs:701-702`). */
    using ChosenColorSpace = Oklcha;

    /** @brief The luminance of this color (Bevy `Luminance for Color`, `color.rs:705-718`). HSV
     *  and HWB have no native definition, so they go through `ChosenColorSpace`. */
    float luminance() const;

    /** @brief Return a copy of this color with the luminance set to the given value, in the
     *  variant's own space (Bevy `Luminance for Color`, `color.rs:720-737`). */
    Color with_luminance(float value) const;

    /** @brief Make the color darker by some amount, in the variant's own space (Bevy `Luminance
     *  for Color`, `color.rs:739-756`). */
    Color darker(float amount) const;

    /** @brief Make the color lighter by some amount, in the variant's own space (Bevy `Luminance
     *  for Color`, `color.rs:758-775`). */
    Color lighter(float amount) const;

    /** @brief Return a copy of this color with the hue channel set to the given value (Bevy `Hue
     *  for Color`, `color.rs:779-796`). HSL, HSV, HWB, LCH and Oklch have a native definition; the
     *  other spaces go through `ChosenColorSpace`. */
    Color with_hue(float hue) const;

    /** @brief Return the hue of this color [0.0, 360.0] (Bevy `Hue for Color`,
     *  `color.rs:798-811`). */
    float hue() const;

    /** @brief Sets the hue of this color (Bevy `Hue for Color`, `color.rs:813-815`). */
    void set_hue(float hue) { *this = with_hue(hue); }

    /** @brief The provided `Hue::rotate_hue` (`color_ops.rs:105-109`): the hue plus `degrees`,
     *  wrapped into `[0.0, 360.0)` by `rem_euclid`. */
    Color rotate_hue(float degrees) const {
        return with_hue(epix::color::rem_euclid(hue() + degrees, 360.0f));
    }

    /** @brief Return a copy of this color with the saturation channel set to the given value (Bevy
     *  `Saturation for Color`, `color.rs:819-834`). HSL and HSV have a native definition; every
     *  other space goes through `Hsla`. */
    Color with_saturation(float saturation) const;

    /** @brief Return the saturation of this color [0.0, 1.0] (Bevy `Saturation for Color`,
     *  `color.rs:836-849`). */
    float saturation() const;

    /** @brief Sets the saturation of this color (Bevy `Saturation for Color`,
     *  `color.rs:851-853`). */
    void set_saturation(float saturation) { *this = with_saturation(saturation); }

    /** @brief Linearly interpolate between this color and another, in the variant's own space
     *  (Bevy `Mix for Color`, `color.rs:857-874`): the other color is converted into the space of
     *  the receiver before mixing. */
    Color mix(const Color& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Color& other, float factor) { *this = mix(other, factor); }

    /** @brief The distance between this and another color as if they were coordinates in a
     *  Euclidean space; alpha is not considered (Bevy `EuclideanDistance for Color`,
     *  `color.rs:878-892`). sRGB, linear RGB, Oklab and Oklch compare in their own space; every
     *  other space goes through `ChosenColorSpace`. */
    float distance_squared(const Color& other) const;

    /** @brief The provided `EuclideanDistance::distance` (`color_difference.rs:7-11`). */
    float distance(const Color& other) const { return std::sqrt(distance_squared(other)); }

    /** @brief Interpolate between two colors that are in the same color space, mixing in that
     *  space, and report a mismatch when they are not (Bevy `TryStableInterpolate for Color`,
     *  `color.rs:894-912`). Bevy's `Result<Self, MismatchedUnitsError>` is the port's
     *  `std::expected`. */
    std::expected<Color, MismatchedUnitsError> try_interpolate_stable(const Color& other,
                                                                     float t) const;
};

}  // namespace epix::color

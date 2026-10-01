#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cstdint>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/color/color_ops.hpp>

namespace epix::color {

/** @brief Color in LCH color space, with alpha (Bevy `Lcha`, `bevy_color/src/lcha.rs:9-34`).
 *
 *  Fields keep Bevy's names and ranges. Rust's derived `Default` is overridden to return
 *  `new(1., 0., 0., 1.)` (`lcha.rs:112-116`), which C++ encodes as the member initialisers below.
 *  Bevy gives `Lcha` no `impl_componentwise_vector_space!` (that macro is invoked only for `Xyza`,
 *  `Srgba`, `LinearRgba`, `Laba` and `Oklaba`), so this type has no arithmetic operators, no
 *  `VectorSpace::ZERO` and no `StableInterpolate`; `lcha.rs:36` is only
 *  `impl StandardColor for Lcha {}`, the crate-internal marker the port omits. The `color_ops.rs`
 *  traits are the members documented on `epix/color/color_ops.hpp`, and Rust's `PartialEq` derive
 *  is the defaulted `operator==`.
 *
 *  LCH is the polar form of LAB: `Lcha` ↔ `Laba` is the only conversion here with a formula of its
 *  own, and everything else in Bevy hops through `Laba`. Note that both `black` and `white` share
 *  the same hue, the hue of the achromatic axis, and that `Mix` interpolates the hue along the
 *  shortest arc with `lerp_hue` rather than componentwise like the other channels.
 *
 *  Bevy implements neither `EuclideanDistance`/`ColorToPacked` nor `Saturation` for `Lcha`, so this
 *  type has no `distance`/`distance_squared`, no `u8` packing and no saturation accessors. Also
 *  omitted, as throughout the port: `#[derive(Reflect)]`/`#[reflect(..)]`, serde, `Debug`, the
 *  GPU-side `Pod`/`Zeroable` impls and the crate-internal `StandardColor` marker trait
 *  (`lib.rs:148-176`). */
EPIX_EXPORT struct EPIX_API Lcha {
    /** @brief The lightness channel. [0.0, 1.5] */
    float lightness = 1.0f;
    /** @brief The chroma channel. [0.0, 1.5] */
    float chroma = 0.0f;
    /** @brief The hue channel, in degrees. [0.0, 360.0] */
    float hue = 0.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Lcha&) const = default;

    constexpr Lcha() = default;

    /** @brief Construct a color from components (Bevy `Lcha::new`, `lcha.rs:47-54`). */
    constexpr Lcha(float lightness, float chroma, float hue, float alpha)
        : lightness(lightness), chroma(chroma), hue(hue), alpha(alpha) {}

    /** @brief A fully black color with full alpha (Bevy `Lcha::BLACK`, `lcha.rs:132`). */
    static const Lcha BLACK;

    /** @brief A fully white color with full alpha (Bevy `Lcha::WHITE`, `lcha.rs:133`). */
    static const Lcha WHITE;

    /** @brief Construct a color from (l, c, h) components, with the default alpha (1.0) (Bevy
     *  `Lcha::lch`, `lcha.rs:63-70`). */
    constexpr static Lcha lch(float lightness, float chroma, float hue) {
        return Lcha(lightness, chroma, hue, 1.0f);
    }

    /** @brief Return a copy of this color with the chroma channel set to the given value (Bevy
     *  `Lcha::with_chroma`, `lcha.rs:73-75`). */
    constexpr Lcha with_chroma(float chroma) const {
        Lcha result   = *this;
        result.chroma = chroma;
        return result;
    }

    /** @brief Return a copy of this color with the lightness channel set to the given value (Bevy
     *  `Lcha::with_lightness`, `lcha.rs:78-80`). */
    constexpr Lcha with_lightness(float lightness) const {
        Lcha result      = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Produce a deterministic, uniformly distributed color for the given index, mostly
     *  useful for debugging (Bevy `Lcha::sequential_dispersed`, `lcha.rs:82-109`): the index is
     *  multiplied by the fractional part of the golden ratio (wrapping in `u32`) and scaled to a
     *  hue. */
    static Lcha sequential_dispersed(std::uint32_t index);

    /** @brief Linearly interpolate between this color and another; the hue travels along the
     *  shortest arc (Bevy `Mix for Lcha`, `lcha.rs:118-129`). */
    Lcha mix(const Lcha& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Lcha& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Lcha gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`, `lcha.rs:136-139`). */
    constexpr Lcha with_alpha(float alpha) const {
        Lcha result  = *this;
        result.alpha = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, `lcha.rs:141-144`, spelled `get_alpha`
     *  because the field keeps that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`, `lcha.rs:146-149`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief A copy of this color with the hue channel set to the given value, in degrees (Bevy
     *  `Hue::with_hue`, `lcha.rs:154-157`). */
    constexpr Lcha with_hue(float hue) const {
        Lcha result = *this;
        result.hue  = hue;
        return result;
    }

    /** @brief The color's hue, in degrees (Bevy `Hue::hue`, `lcha.rs:159-162`, spelled `get_hue`
     *  because the field keeps that name). */
    constexpr float get_hue() const { return hue; }

    /** @brief Set the color's hue, in degrees (Bevy `Hue::set_hue`, `lcha.rs:164-167`). */
    constexpr void set_hue(float hue) { this->hue = hue; }

    /** @brief The provided `Hue::rotate_hue` (`color_ops.rs:95-110`): rotate the hue by the given
     *  number of degrees, wrapping around the hue circle. */
    Lcha rotate_hue(float degrees) const { return with_hue(rem_euclid(get_hue() + degrees, 360.0f)); }

    /** @brief The luminance, which in this space *is* the lightness channel (Bevy `Luminance for
     *  Lcha`, `lcha.rs:190-192`). */
    constexpr float luminance() const { return lightness; }

    /** @brief A copy of this color with the luminance (the lightness channel) set to the given
     *  value (Bevy `Lcha::with_luminance`, `lcha.rs:173-175`). */
    constexpr Lcha with_luminance(float lightness) const {
        Lcha result      = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Make the color darker by some amount, flooring the lightness at 0.0 (Bevy
     *  `Lcha::darker`, `lcha.rs:177-184`). */
    constexpr Lcha darker(float amount) const {
        return Lcha(std::max(lightness - amount, 0.0f), chroma, hue, alpha);
    }

    /** @brief Make the color lighter by some amount, capping the lightness at 1.0 (Bevy
     *  `Lcha::lighter`, `lcha.rs:186-193`). */
    constexpr Lcha lighter(float amount) const {
        return Lcha(std::min(lightness + amount, 1.0f), chroma, hue, alpha);
    }

    /** @brief The components as an LCH array (Bevy `ColorToComponents::to_f32_array`,
     *  `lcha.rs:199-206`; the port drops the `to_` prefix, so the constructors below and these
     *  accessors are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {lightness, chroma, hue, alpha}; }

    /** @brief The components as an LCH array without alpha (Bevy
     *  `ColorToComponents::to_f32_array_no_alpha`, `lcha.rs:200-206`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {lightness, chroma, hue}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `lcha.rs:208-210`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(lightness, chroma, hue, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `lcha.rs:212-214`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(lightness, chroma, hue); }

    /** @brief Build a color from an LCH array (Bevy `Lcha::from_f32_array`, `lcha.rs:216-223`). */
    constexpr Lcha(const std::array<float, 4>& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an LCH array, with full alpha (Bevy `Lcha::from_f32_array_no_alpha`,
     *  `lcha.rs:225-232`). */
    constexpr Lcha(const std::array<float, 3>& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `Lcha::from_vec4`, `lcha.rs:234-241`). */
    constexpr Lcha(const glm::vec4& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy `Lcha::from_vec3`,
     *  `lcha.rs:243-250`). */
    constexpr Lcha(const glm::vec3& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Lcha` blocks over the file of the type being converted
    // from; the port defines every conversion that *produces* an `Lcha` here, as this type's
    // converting constructors. The `Laba` pair is the only one with a formula of its own; everything
    // else follows Bevy's own delegation, which hops through `Laba` for the RGB-shaped spaces and
    // through `Oklaba` for the OKLab ones.

    /** @brief Bevy `impl From<Laba> for Lcha` (`lcha.rs:272-297`): the polar Lab → LCH conversion,
     *  `chroma = hypot(a, b)` (clamped into [0.0, 1.5]) and `hue = atan2` in degrees, shifted into
     *  [0, 360). */
    Lcha(const Laba& value);

    /** @brief Bevy `impl From<Srgba> for Lcha` (`lcha.rs:302-306`): hops through `Laba`. */
    Lcha(const Srgba& value);

    /** @brief Bevy `impl From<LinearRgba> for Lcha` (`lcha.rs:314-318`): hops through `Laba`. */
    Lcha(const LinearRgba& value);

    /** @brief Bevy `impl From<Xyza> for Lcha` (`lcha.rs:326-330`): hops through `Laba`. */
    Lcha(const Xyza& value);

    /** @brief Bevy `impl From<Hsla> for Lcha` (`hsla.rs:351-355`): hops through `Hsva`. */
    Lcha(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Lcha` (`hsva.rs:281-285`): hops through `Hwba`. */
    Lcha(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Lcha` (`hwba.rs:289-293`): hops through `Srgba`. */
    Lcha(const Hwba& value);

    /** @brief Bevy `impl From<Oklaba> for Lcha` (`oklaba.rs:322-326`): hops through
     *  `LinearRgba`. */
    Lcha(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Lcha` (`oklcha.rs:349-353`): hops through `Oklaba`. */
    Lcha(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Lcha` (`color.rs:633-648`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Lcha(const Color& value);
};

}  // namespace epix::color

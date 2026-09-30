#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <epix/color/color_ops.hpp>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::color {

/** @brief Color in Oklch color space, with alpha (Bevy `Oklcha`,
 *  `bevy_color/src/oklcha.rs:25-34`).
 *
 *  Fields keep Bevy's names and ranges. Rust's `Default` (`oklcha.rs:107-111`) returns
 *  `Oklcha::new(1., 0., 0., 1.)`, which C++ encodes as the member initialisers below.
 *  `Oklcha` is not one of the spaces Bevy gives `impl_componentwise_vector_space!` (only `Srgba`,
 *  `LinearRgba`, `Laba`, `Xyza` and `Oklaba` have it), so this type has no arithmetic operators
 *  and no `ZERO`; the `color_ops.rs` traits below are the members documented in
 *  `epix/color/color_ops.hpp`, and Rust's `PartialEq` derive is the defaulted `operator==`.
 *
 *  Omitted with the rest of the crate: `Reflect`/`#[reflect(..)]`, serde, `Debug` and the
 *  GPU-side `Pod`/`Zeroable` impls (`oklcha.rs:6-24`), plus the crate-internal `StandardColor`
 *  marker (`oklcha.rs:36`). Bevy implements `ColorToPacked` for `Srgba` and `LinearRgba` only, so
 *  an `Oklcha` has no `u8_array` family. */
EPIX_EXPORT struct EPIX_API Oklcha {
    /** @brief The 'lightness' channel. [0.0, 1.0] */
    float lightness = 1.0f;
    /** @brief The 'chroma' channel. [0.0, 1.0] */
    float chroma = 0.0f;
    /** @brief The 'hue' channel. [0.0, 360.0] */
    float hue = 0.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Oklcha&) const = default;

    constexpr Oklcha() = default;

    /** @brief Construct a color from components (Bevy `Oklcha::new`, `oklcha.rs:47-54`). */
    constexpr Oklcha(float lightness, float chroma, float hue, float alpha)
        : lightness(lightness), chroma(chroma), hue(hue), alpha(alpha) {}

    /** @brief A pure black color (Bevy `Gray for Oklcha::BLACK`, `oklcha.rs:126-129`). */
    static const Oklcha BLACK;

    /** @brief A pure white color (Bevy `Gray for Oklcha::WHITE`, `oklcha.rs:126-129`); the tiny
     *  chroma and the 90-degree hue are Bevy's own values, not transcription slips. */
    static const Oklcha WHITE;

    /** @brief Construct a color from (l, c, h) components, with the default alpha (1.0) (Bevy
     *  `Oklcha::lch`, `oklcha.rs:63-65`). */
    constexpr static Oklcha lch(float lightness, float chroma, float hue) {
        return Oklcha(lightness, chroma, hue, 1.0f);
    }

    /** @brief Return a copy of this color with the 'lightness' channel set to the given value
     *  (Bevy `Oklcha::with_lightness`, `oklcha.rs:68-70`). */
    constexpr Oklcha with_lightness(float lightness) const {
        Oklcha result    = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Return a copy of this color with the 'chroma' channel set to the given value (Bevy
     *  `Oklcha::with_chroma`, `oklcha.rs:73-75`). */
    constexpr Oklcha with_chroma(float chroma) const {
        Oklcha result = *this;
        result.chroma = chroma;
        return result;
    }

    /** @brief Generate a deterministic but quasi-randomly distributed color from a provided
     *  `index` (Bevy `Oklcha::sequential_dispersed`, `oklcha.rs:94-104`); useful for debug
     *  colors. The wrapped multiply and the `u32::MAX` ratio are Bevy's, so the sequence matches
     *  byte for byte. */
    constexpr static Oklcha sequential_dispersed(std::uint32_t index) {
        // (u32::MAX / Φ) rounded up
        constexpr std::uint32_t FRAC_U32MAX_GOLDEN_RATIO = 2654435769u;
        constexpr float RATIO_360 = 360.0f / static_cast<float>(UINT32_MAX);

        // from https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
        //
        // Map a sequence of integers (eg: 154, 155, 156, 157, 158) into the [0.0..1.0] range,
        // so that the closer the numbers are, the larger the difference of their image.
        const float hue = static_cast<float>(index * FRAC_U32MAX_GOLDEN_RATIO) * RATIO_360;
        return Oklcha::lch(0.75f, 0.1f, hue);
    }

    /** @brief The luminance of this color (0.0 - 1.0) (Bevy `Luminance for Oklcha`,
     *  `oklcha.rs:171-173`); Oklch's lightness *is* its luminance. */
    constexpr float luminance() const { return lightness; }

    /** @brief A copy of this color with the luminance set to the given value (Bevy
     *  `Luminance::with_luminance`, `oklcha.rs:167-170`). */
    constexpr Oklcha with_luminance(float lightness) const {
        Oklcha result    = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief A darker version of this color (Bevy `Luminance::darker`, `oklcha.rs:175-182`);
     *  lightness is clamped to black. */
    constexpr Oklcha darker(float amount) const {
        return Oklcha(std::max(lightness - amount, 0.0f), chroma, hue, alpha);
    }

    /** @brief A lighter version of this color (Bevy `Luminance::lighter`, `oklcha.rs:184-191`);
     *  lightness is clamped to white. */
    constexpr Oklcha lighter(float amount) const {
        return Oklcha(std::min(lightness + amount, 1.0f), chroma, hue, alpha);
    }

    /** @brief Linearly interpolate between this color and another (Bevy `Mix for Oklcha`,
     *  `oklcha.rs:113-124`); the hue runs along the shortest arc. */
    Oklcha mix(const Oklcha& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Oklcha& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Oklcha gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`,
     *  `oklcha.rs:131-146`). */
    constexpr Oklcha with_alpha(float alpha) const {
        Oklcha result = *this;
        result.alpha  = alpha;
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

    /** @brief A copy of this color with the hue channel set to the given value (Bevy `Hue for
     *  Oklcha`, `oklcha.rs:150-152`). */
    constexpr Oklcha with_hue(float hue) const {
        Oklcha result = *this;
        result.hue    = hue;
        return result;
    }

    /** @brief The hue of this color [0.0, 360.0] (Bevy `Hue::hue`, spelled `get_hue` because the
     *  field keeps that name). */
    constexpr float get_hue() const { return hue; }

    /** @brief Set the hue of this color (Bevy `Hue::set_hue`). */
    constexpr void set_hue(float hue) { this->hue = hue; }

    /** @brief A copy of this color with the hue channel rotated by the given degrees (Bevy
     *  `Hue::rotate_hue`, `color_ops.rs:106-109`). */
    Oklcha rotate_hue(float degrees) const { return with_hue(rem_euclid(get_hue() + degrees, 360.0f)); }

    /** @brief The distance between this and another color as if they were coordinates in a
     *  Euclidean space; alpha is not considered (Bevy `EuclideanDistance for Oklcha`,
     *  `oklcha.rs:194-201`). */
    float distance_squared(const Oklcha& other) const;

    /** @brief The provided `EuclideanDistance::distance` (`color_difference.rs:7-11`). */
    float distance(const Oklcha& other) const { return std::sqrt(distance_squared(other)); }

    /** @brief The components as an `(l, c, h, alpha)` array (Bevy
     *  `ColorToComponents::to_f32_array`, `oklcha.rs:203-255`; the port drops the `to_` prefix, so
     *  the constructors below and this accessor are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {lightness, chroma, hue, alpha}; }

    /** @brief The components as an `(l, c, h)` array (Bevy
     *  `ColorToComponents::to_f32_array_no_alpha`, `oklcha.rs:208-210`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {lightness, chroma, hue}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `oklcha.rs:212-214`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(lightness, chroma, hue, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `oklcha.rs:216-218`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(lightness, chroma, hue); }

    /** @brief Build a color from an `(l, c, h, alpha)` array (Bevy `Oklcha::from_f32_array`,
     *  `oklcha.rs:220-227`). */
    Oklcha(const std::array<float, 4>& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an `(l, c, h)` array, with full alpha (Bevy
     *  `Oklcha::from_f32_array_no_alpha`, `oklcha.rs:229-236`). */
    Oklcha(const std::array<float, 3>& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `Oklcha::from_vec4`, `oklcha.rs:238-245`). */
    Oklcha(const glm::vec4& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy `Oklcha::from_vec3`,
     *  `oklcha.rs:247-254`). */
    Oklcha(const glm::vec3& color)
        : lightness(color[0]), chroma(color[1]), hue(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Oklcha` blocks over the file of the type being
    // converted from; the port defines every conversion that *produces* an `Oklcha` here, as this
    // type's converting constructor. The delegation order is Bevy's own: every other space hops
    // through `Oklaba`, and `Oklaba` is the rectangular half of the `Oklaba`/`Oklcha` pair.

    /** @brief Bevy `impl From<Oklaba> for Oklcha` (`oklcha.rs:257-273`): the rectangular → polar
     *  half, `chroma = hypot(a, b)`, `hue = atan2(b, a)` in degrees, wrapped into `[0, 360)`. */
    Oklcha(const Oklaba& value);

    /** @brief Bevy `impl From<Hsla> for Oklcha` (`oklcha.rs:295-299`): hops through `Oklaba`. */
    Oklcha(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Oklcha` (`oklcha.rs:307-311`): hops through `Oklaba`. */
    Oklcha(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Oklcha` (`oklcha.rs:319-323`): hops through `Oklaba`. */
    Oklcha(const Hwba& value);

    /** @brief Bevy `impl From<Laba> for Oklcha` (`oklcha.rs:331-335`): hops through `Oklaba`. */
    Oklcha(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for Oklcha` (`oklcha.rs:343-347`): hops through `Oklaba`. */
    Oklcha(const Lcha& value);

    /** @brief Bevy `impl From<LinearRgba> for Oklcha` (`oklcha.rs:355-359`): hops through
     *  `Oklaba`. */
    Oklcha(const LinearRgba& value);

    /** @brief Bevy `impl From<Srgba> for Oklcha` (`oklcha.rs:367-371`): hops through `Oklaba`. */
    Oklcha(const Srgba& value);

    /** @brief Bevy `impl From<Xyza> for Oklcha` (`oklcha.rs:379-383`): hops through `Oklaba`. */
    Oklcha(const Xyza& value);

    /** @brief Bevy `impl From<Color> for Oklcha` (`color.rs:667-682`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Oklcha(const Color& value);
};

}  // namespace epix::color

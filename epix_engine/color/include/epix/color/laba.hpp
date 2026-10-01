#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/color/color_ops.hpp>

namespace epix::color {

/** @brief Color in LAB color space, with alpha (Bevy `Laba`, `bevy_color/src/laba.rs:9-34`).
 *
 *  Fields keep Bevy's names and ranges. Rust's derived `Default` is overridden to return
 *  `new(1., 0., 0., 1.)` (`laba.rs:90-94`), which C++ encodes as the member initialisers below.
 *  `impl_componentwise_vector_space!` (`laba.rs:38`) is expanded by
 *  `EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE`; the `color_ops.rs` traits are the members documented on
 *  that macro, and Rust's `PartialEq` derive is the defaulted `operator==`.
 *
 *  `Laba` is defined relative to `Xyza::D65_WHITE`; the XYZ ↔ Lab pair is the only conversion in
 *  this space with a formula of its own, everything else in Bevy either hops through `Xyza` or, in
 *  `Lcha`'s case, converts the polar chroma/hue pair with `cos`/`sin`.
 *
 *  Bevy implements neither `EuclideanDistance` (`color_difference.rs`) nor `ColorToPacked` for
 *  `Laba`, so this type has no `distance`/`distance_squared` and no `u8` packing. Also omitted, as
 *  throughout the port: `#[derive(Reflect)]`/`#[reflect(..)]`, serde, `Debug`, the GPU-side
 *  `Pod`/`Zeroable` impls and the crate-internal `StandardColor` marker trait (`lib.rs:148-176`). */
EPIX_EXPORT struct EPIX_API Laba {
    /** @brief The lightness channel. [0.0, 1.5] */
    float lightness = 1.0f;
    /** @brief The a axis. [-1.5, 1.5] */
    float a = 0.0f;
    /** @brief The b axis. [-1.5, 1.5] */
    float b = 0.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Laba&) const = default;

    constexpr Laba() = default;

    /** @brief Construct a color from components (Bevy `Laba::new`, `laba.rs:49-56`). */
    constexpr Laba(float lightness, float a, float b, float alpha)
        : lightness(lightness), a(a), b(b), alpha(alpha) {}

    EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE(Laba, lightness, a, b, alpha)

    /** @brief CIE Epsilon Constant, the `(6/29)^3` threshold below which the XYZ ↔ Lab transfer
     *  function is linear (Bevy `Laba::CIE_EPSILON`, `laba.rs:79-82`; see
     *  [Continuity (16) (17)](http://brucelindbloom.com/index.html?LContinuity.html)). */
    inline static constexpr float CIE_EPSILON = 216.0f / 24389.0f;

    /** @brief CIE Kappa Constant, the `(29/3)^3` slope of that linear segment (Bevy
     *  `Laba::CIE_KAPPA`, `laba.rs:84-87`). */
    inline static constexpr float CIE_KAPPA = 24389.0f / 27.0f;

    /** @brief A fully black color with full alpha (Bevy `Laba::BLACK`, `laba.rs:110`). */
    static const Laba BLACK;

    /** @brief A fully white color with full alpha (Bevy `Laba::WHITE`, `laba.rs:111`). */
    static const Laba WHITE;

    /** @brief Construct a color from (l, a, b) components, with the default alpha (1.0) (Bevy
     *  `Laba::lab`, `laba.rs:65-72`). */
    constexpr static Laba lab(float lightness, float a, float b) { return Laba(lightness, a, b, 1.0f); }

    /** @brief Return a copy of this color with the lightness channel set to the given value (Bevy
     *  `Laba::with_lightness`, `laba.rs:75-77`). */
    constexpr Laba with_lightness(float lightness) const {
        Laba result      = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Linearly interpolate between this color and another, componentwise (Bevy `Mix for
     *  Laba`, `laba.rs:96-107`). */
    Laba mix(const Laba& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Laba& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Laba gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`, `laba.rs:115-118`). */
    constexpr Laba with_alpha(float alpha) const {
        Laba result  = *this;
        result.alpha = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, `laba.rs:120-123`, spelled `get_alpha`
     *  because the field keeps that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`, `laba.rs:125-128`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief The luminance, which in this space *is* the lightness channel (Bevy `Luminance for
     *  Laba`, `laba.rs:137-139`). */
    constexpr float luminance() const { return lightness; }

    /** @brief A copy of this color with the luminance (the lightness channel) set to the given
     *  value (Bevy `Laba::with_luminance`, `laba.rs:132-134`). */
    constexpr Laba with_luminance(float lightness) const {
        Laba result      = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Make the color darker by some amount, flooring the lightness at 0.0 (Bevy
     *  `Laba::darker`, `laba.rs:141-148`). */
    constexpr Laba darker(float amount) const {
        return Laba(std::max(lightness - amount, 0.0f), a, b, alpha);
    }

    /** @brief Make the color lighter by some amount, capping the lightness at 1.0 (Bevy
     *  `Laba::lighter`, `laba.rs:150-157`). */
    constexpr Laba lighter(float amount) const {
        return Laba(std::min(lightness + amount, 1.0f), a, b, alpha);
    }

    /** @brief The components as an LAB array (Bevy `ColorToComponents::to_f32_array`,
     *  `laba.rs:160-167`; the port drops the `to_` prefix, so the constructors below and these
     *  accessors are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {lightness, a, b, alpha}; }

    /** @brief The components as an LAB array without alpha (Bevy
     *  `ColorToComponents::to_f32_array_no_alpha`, `laba.rs:161-167`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {lightness, a, b}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `laba.rs:169-171`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(lightness, a, b, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `laba.rs:173-175`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(lightness, a, b); }

    /** @brief Build a color from an LAB array (Bevy `Laba::from_f32_array`, `laba.rs:177-184`). */
    constexpr Laba(const std::array<float, 4>& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an LAB array, with full alpha (Bevy `Laba::from_f32_array_no_alpha`,
     *  `laba.rs:186-193`). */
    constexpr Laba(const std::array<float, 3>& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `Laba::from_vec4`, `laba.rs:195-202`). */
    constexpr Laba(const glm::vec4& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy `Laba::from_vec3`,
     *  `laba.rs:204-211`). */
    constexpr Laba(const glm::vec3& color) : lightness(color[0]), a(color[1]), b(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Laba` blocks over the file of the type being converted
    // from; the port defines every conversion that *produces* a `Laba` here, as this type's
    // converting constructors. Eight of them hop through the XYZ ↔ Lab pair defined in `laba.cpp`,
    // which is the only lab conversion with a formula of its own: Bevy's own derived conversions in
    // `laba.rs:291-363` all read `Xyza::from(value).into()`, and `Oklcha` is the one extra hop
    // (`Oklaba` first).

    /** @brief Bevy `impl From<Xyza> for Laba` (`laba.rs:262-289`): the XYZ → Lab formula, with the
     *  `CIE_EPSILON`/`CIE_KAPPA` branch on each of `xr`/`yr`/`zr`. */
    Laba(const Xyza& value);

    /** @brief Bevy `impl From<Srgba> for Laba` (`laba.rs:293-297`): hops through `Xyza`. */
    Laba(const Srgba& value);

    /** @brief Bevy `impl From<LinearRgba> for Laba` (`laba.rs:305-309`): hops through `Xyza`. */
    Laba(const LinearRgba& value);

    /** @brief Bevy `impl From<Hsla> for Laba` (`laba.rs:317-321`): hops through `Xyza`. */
    Laba(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Laba` (`laba.rs:329-333`): hops through `Xyza`. */
    Laba(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Laba` (`laba.rs:341-345`): hops through `Xyza`. */
    Laba(const Hwba& value);

    /** @brief Bevy `impl From<Oklaba> for Laba` (`laba.rs:353-357`): hops through `Xyza`. */
    Laba(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Laba` (`oklcha.rs:337-341`): hops through `Oklaba`. */
    Laba(const Oklcha& value);

    /** @brief Bevy `impl From<Lcha> for Laba` (`lcha.rs:253-269`): the polar LCH → Lab conversion,
     *  `a = chroma * cos(hue)` and `b = chroma * sin(hue)` with `hue` in radians. */
    Laba(const Lcha& value);

    /** @brief Bevy `impl From<Color> for Laba` (`color.rs:616-631`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Laba(const Color& value);
};

}  // namespace epix::color

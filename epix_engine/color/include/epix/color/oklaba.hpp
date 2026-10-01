#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/color/color_ops.hpp>

namespace epix::color {

/** @brief Color in Oklab color space, with alpha (Bevy `Oklaba`,
 *  `bevy_color/src/oklaba.rs:25-34`).
 *
 *  Fields keep Bevy's names and ranges. Rust's `Default` (`oklaba.rs:90-94`) returns
 *  `Oklaba::new(1., 0., 0., 1.)`, which C++ encodes as the member initialisers below.
 *  `impl_componentwise_vector_space!` (`oklaba.rs:38`) is expanded by
 *  `EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE`; the `color_ops.rs` traits are the members documented
 *  on that macro, and Rust's `PartialEq` derive is the defaulted `operator==`.
 *
 *  Omitted with the rest of the crate: `Reflect`/`#[reflect(..)]`, serde, `Debug` and the
 *  GPU-side `Pod`/`Zeroable` impls (`oklaba.rs:6-24`), plus the crate-internal `StandardColor`
 *  marker (`oklaba.rs:36`). Bevy implements `ColorToPacked` for `Srgba` and `LinearRgba` only, so
 *  an `Oklaba` has no `u8_array` family. */
EPIX_EXPORT struct EPIX_API Oklaba {
    /** @brief The 'lightness' channel. [0.0, 1.0] */
    float lightness = 1.0f;
    /** @brief The 'a' channel. [-1.0, 1.0] */
    float a = 0.0f;
    /** @brief The 'b' channel. [-1.0, 1.0] */
    float b = 0.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Oklaba&) const = default;

    constexpr Oklaba() = default;

    /** @brief Construct a color from components (Bevy `Oklaba::new`, `oklaba.rs:49-56`). */
    constexpr Oklaba(float lightness, float a, float b, float alpha)
        : lightness(lightness), a(a), b(b), alpha(alpha) {}

    EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE(Oklaba, lightness, a, b, alpha)

    /** @brief A pure black color (Bevy `Gray for Oklaba::BLACK`, `oklaba.rs:109-112`). */
    static const Oklaba BLACK;

    /** @brief A pure white color (Bevy `Gray for Oklaba::WHITE`, `oklaba.rs:109-112`); the tiny
     *  `b` channel is Bevy's own value, not a transcription slip. */
    static const Oklaba WHITE;

    /** @brief Construct a color from (l, a, b) components, with the default alpha (1.0) (Bevy
     *  `Oklaba::lab`, `oklaba.rs:65-72`). */
    constexpr static Oklaba lab(float lightness, float a, float b) {
        return Oklaba(lightness, a, b, 1.0f);
    }

    /** @brief Return a copy of this color with the 'lightness' channel set to the given value
     *  (Bevy `Oklaba::with_lightness`, `oklaba.rs:75-77`). */
    constexpr Oklaba with_lightness(float lightness) const {
        Oklaba result    = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Return a copy of this color with the 'a' channel set to the given value (Bevy
     *  `Oklaba::with_a`, `oklaba.rs:80-82`). */
    constexpr Oklaba with_a(float a) const {
        Oklaba result = *this;
        result.a      = a;
        return result;
    }

    /** @brief Return a copy of this color with the 'b' channel set to the given value (Bevy
     *  `Oklaba::with_b`, `oklaba.rs:85-87`). */
    constexpr Oklaba with_b(float b) const {
        Oklaba result = *this;
        result.b      = b;
        return result;
    }

    /** @brief The luminance of this color (0.0 - 1.0) (Bevy `Luminance for Oklaba`,
     *  `oklaba.rs:137-139`); Oklab's lightness *is* its luminance. */
    constexpr float luminance() const { return lightness; }

    /** @brief A copy of this color with the luminance set to the given value (Bevy
     *  `Luminance::with_luminance`, `oklaba.rs:132-135`). */
    constexpr Oklaba with_luminance(float lightness) const {
        Oklaba result    = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief A darker version of this color (Bevy `Luminance::darker`, `oklaba.rs:141-148`);
     *  lightness is clamped to black. */
    constexpr Oklaba darker(float amount) const {
        return Oklaba(std::max(lightness - amount, 0.0f), a, b, alpha);
    }

    /** @brief A lighter version of this color (Bevy `Luminance::lighter`, `oklaba.rs:150-157`);
     *  lightness is clamped to white. */
    constexpr Oklaba lighter(float amount) const {
        return Oklaba(std::min(lightness + amount, 1.0f), a, b, alpha);
    }

    /** @brief Linearly interpolate between this color and another (Bevy `Mix for Oklaba`,
     *  `oklaba.rs:96-107`). */
    Oklaba mix(const Oklaba& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Oklaba& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Oklaba gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`,
     *  `oklaba.rs:114-129`). */
    constexpr Oklaba with_alpha(float alpha) const {
        Oklaba result = *this;
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

    /** @brief The distance between this and another color as if they were coordinates in a
     *  Euclidean space; alpha is not considered (Bevy `EuclideanDistance for Oklaba`,
     *  `oklaba.rs:160-167`). */
    float distance_squared(const Oklaba& other) const;

    /** @brief The provided `EuclideanDistance::distance` (`color_difference.rs:7-11`). */
    float distance(const Oklaba& other) const { return std::sqrt(distance_squared(other)); }

    /** @brief The components as an `(l, a, b, alpha)` array (Bevy
     *  `ColorToComponents::to_f32_array`, `oklaba.rs:169-221`; the port drops the `to_` prefix, so
     *  the constructors below and this accessor are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {lightness, a, b, alpha}; }

    /** @brief The components as an `(l, a, b)` array (Bevy
     *  `ColorToComponents::to_f32_array_no_alpha`, `oklaba.rs:174-176`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {lightness, a, b}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `oklaba.rs:178-180`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(lightness, a, b, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `oklaba.rs:182-184`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(lightness, a, b); }

    /** @brief Build a color from an `(l, a, b, alpha)` array (Bevy `Oklaba::from_f32_array`,
     *  `oklaba.rs:186-193`). */
    Oklaba(const std::array<float, 4>& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an `(l, a, b)` array, with full alpha (Bevy
     *  `Oklaba::from_f32_array_no_alpha`, `oklaba.rs:195-202`). */
    Oklaba(const std::array<float, 3>& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `Oklaba::from_vec4`, `oklaba.rs:204-211`). */
    Oklaba(const glm::vec4& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy `Oklaba::from_vec3`,
     *  `oklaba.rs:213-220`). */
    Oklaba(const glm::vec3& color)
        : lightness(color[0]), a(color[1]), b(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Oklaba` blocks over the file of the type being
    // converted from; the port defines every conversion that *produces* an `Oklaba` here, as this
    // type's converting constructor, so a reader asking "how do I get an Oklab colour" has one
    // place to look. The delegation order is Bevy's own: the hue-based spaces plus `Srgba` and
    // `Xyza` hop through `LinearRgba`, `Laba` through `Xyza`, and `Oklcha` is the polar half of the
    // `Oklaba`/`Oklcha` pair.

    /** @brief Bevy `impl From<LinearRgba> for Oklaba` (`oklaba.rs:223-244`): the linear sRGB →
     *  Oklab matrix, with the intermediate LMS values cube-rooted. */
    Oklaba(const LinearRgba& value);

    /** @brief Bevy `impl From<Hsla> for Oklaba` (`oklaba.rs:280-284`): hops through
     *  `LinearRgba`. */
    Oklaba(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Oklaba` (`oklaba.rs:292-296`): hops through
     *  `LinearRgba`. */
    Oklaba(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Oklaba` (`oklaba.rs:304-308`): hops through
     *  `LinearRgba`. */
    Oklaba(const Hwba& value);

    /** @brief Bevy `impl From<Lcha> for Oklaba` (`oklaba.rs:316-320`): hops through
     *  `LinearRgba`. */
    Oklaba(const Lcha& value);

    /** @brief Bevy `impl From<Srgba> for Oklaba` (`oklaba.rs:328-332`): hops through
     *  `LinearRgba`. */
    Oklaba(const Srgba& value);

    /** @brief Bevy `impl From<Xyza> for Oklaba` (`oklaba.rs:340-344`): hops through
     *  `LinearRgba`. */
    Oklaba(const Xyza& value);

    /** @brief Bevy `impl From<Laba> for Oklaba` (`laba.rs:359-363`): hops through `Xyza`, not
     *  through `LinearRgba`. */
    Oklaba(const Laba& value);

    /** @brief Bevy `impl From<Oklcha> for Oklaba` (`oklcha.rs:275-291`): the polar → rectangular
     *  half, `a = chroma * cos(hue)`, `b = chroma * sin(hue)`. */
    Oklaba(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Oklaba` (`color.rs:650-665`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Oklaba(const Color& value);
};

}  // namespace epix::color

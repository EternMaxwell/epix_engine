#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif
#include <epix/color/color_ops.hpp>

namespace epix::color {

/** @brief Color in Hue-Saturation-Lightness (HSL) color space with alpha (Bevy `Hsla`,
 *  `bevy_color/src/hsla.rs:26-35`). Further information on this color model can be found on
 *  [Wikipedia](https://en.wikipedia.org/wiki/HSL_and_HSV).
 *
 *  Fields keep Bevy's names and ranges. Rust's derived `Default` is overridden to return
 *  `Self::new(0., 0., 1., 1.)` (`hsla.rs:108-112`), a pure white, which is what the member
 *  initialisers below encode. Bevy never calls `impl_componentwise_vector_space!` for this space
 *  (`bevy_color/src/lib.rs:178-277`), so unlike `LinearRgba` there is no componentwise arithmetic
 *  and no `ZERO`; the `color_ops.rs` traits are ordinary members with Bevy's names, as
 *  `epix/color/color_ops.hpp` documents, and Rust's `PartialEq` derive is the defaulted
 *  `operator==`. `Reflect`, serde and the crate-internal `StandardColor` marker trait are omitted,
 *  as throughout this module (`epix/color.hpp`). */
EPIX_EXPORT struct EPIX_API Hsla {
    /** @brief The hue channel. [0.0, 360.0] */
    float hue = 0.0f;
    /** @brief The saturation channel. [0.0, 1.0] */
    float saturation = 0.0f;
    /** @brief The lightness channel. [0.0, 1.0] */
    float lightness = 1.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Hsla&) const = default;

    constexpr Hsla() = default;

    /** @brief Construct a new `Hsla` color from components (Bevy `Hsla::new`,
     *  `bevy_color/src/hsla.rs:39-55`). */
    constexpr Hsla(float hue, float saturation, float lightness, float alpha)
        : hue(hue), saturation(saturation), lightness(lightness), alpha(alpha) {}

    /** @brief Construct a new `Hsla` color from (h, s, l) components, with the default alpha (1.0)
     *  (Bevy `Hsla::hsl`, `bevy_color/src/hsla.rs:57-66`). */
    constexpr static Hsla hsl(float hue, float saturation, float lightness) {
        return Hsla(hue, saturation, lightness, 1.0f);
    }

    /** @brief Return a copy of this color with the saturation channel set to the given value; both
     *  the inherent `Hsla::with_saturation` and `Saturation::with_saturation`, which Bevy writes
     *  with this same body (`bevy_color/src/hsla.rs:68-71` and `:166-173`). */
    constexpr Hsla with_saturation(float saturation) const {
        Hsla result       = *this;
        result.saturation = saturation;
        return result;
    }

    /** @brief Return a copy of this color with the lightness channel set to the given value (Bevy
     *  `Hsla::with_lightness`, `bevy_color/src/hsla.rs:73-76`). */
    constexpr Hsla with_lightness(float lightness) const {
        Hsla result      = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief Generate a deterministic but
     *  [quasi-randomly distributed](https://en.wikipedia.org/wiki/Low-discrepancy_sequence) color
     *  from a provided `index`, which can be helpful for generating debug colors (Bevy
     *  `Hsla::sequential_dispersed`, `bevy_color/src/hsla.rs:78-105`).
     *
     *  The hue is `index.wrapping_mul(2654435769) as f32 * (360.0 / u32::MAX as f32)`, the
     *  sequence from <https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/>;
     *  C++'s unsigned `*` wraps exactly as Rust's `wrapping_mul`, so the two agree. */
    constexpr static Hsla sequential_dispersed(std::uint32_t index) {
        constexpr std::uint32_t FRAC_U32MAX_GOLDEN_RATIO = 2654435769u;  // (u32::MAX / Φ) rounded up
        constexpr float RATIO_360 = 360.0f / static_cast<float>(std::numeric_limits<std::uint32_t>::max());

        // Map a sequence of integers (eg: 154, 155, 156, 157, 158) into the [0.0..1.0] range,
        // so that the closer the numbers are, the larger the difference of their image.
        const float hue = static_cast<float>(index * FRAC_U32MAX_GOLDEN_RATIO) * RATIO_360;
        return Hsla::hsl(hue, 1.0f, 0.5f);
    }

    /** @brief A pure black color with full alpha, the `Gray::BLACK` associated constant (Bevy
     *  `Gray for Hsla`, `bevy_color/src/hsla.rs:127-130`). */
    static const Hsla BLACK;

    /** @brief A pure white color with full alpha, the `Gray::WHITE` associated constant (Bevy
     *  `Gray for Hsla`, `bevy_color/src/hsla.rs:127-130`). */
    static const Hsla WHITE;

    /** @brief Linearly interpolate between this color and another, the hue along the shortest arc
     *  (Bevy `Mix for Hsla`, `bevy_color/src/hsla.rs:114-125`). */
    Hsla mix(const Hsla& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Hsla& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Hsla gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`,
     *  `bevy_color/src/hsla.rs:132-136`). */
    constexpr Hsla with_alpha(float alpha) const {
        Hsla result  = *this;
        result.alpha = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, `bevy_color/src/hsla.rs:138-141`; spelled
     *  `get_alpha` because the field keeps that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`,
     *  `bevy_color/src/hsla.rs:143-146`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief A copy of this color with the given hue (Bevy `Hue::with_hue`,
     *  `bevy_color/src/hsla.rs:149-153`). */
    constexpr Hsla with_hue(float hue) const {
        Hsla result = *this;
        result.hue  = hue;
        return result;
    }

    /** @brief The color's hue in `[0.0, 360.0]` (Bevy `Hue::hue`,
     *  `bevy_color/src/hsla.rs:155-158`; spelled `get_hue` because the field keeps that name). */
    constexpr float get_hue() const { return hue; }

    /** @brief Set the color's hue (Bevy `Hue::set_hue`, `bevy_color/src/hsla.rs:160-163`). */
    constexpr void set_hue(float hue) { this->hue = hue; }

    /** @brief A copy of this color with the hue rotated by the given degrees (Bevy
     *  `Hue::rotate_hue`, `color_ops.rs:106-109`). */
    Hsla rotate_hue(float degrees) const { return with_hue(rem_euclid(get_hue() + degrees, 360.0f)); }

    /** @brief The color's saturation in `[0.0, 1.0]` (Bevy `Saturation::saturation`,
     *  `bevy_color/src/hsla.rs:175-178`; spelled `get_saturation` because the field keeps that
     *  name). */
    constexpr float get_saturation() const { return saturation; }

    /** @brief Set the color's saturation (Bevy `Saturation::set_saturation`,
     *  `bevy_color/src/hsla.rs:180-183`). */
    constexpr void set_saturation(float saturation) { this->saturation = saturation; }

    /** @brief The luminance of this color; for HSL that is simply its lightness (Bevy
     *  `Luminance for Hsla`, `bevy_color/src/hsla.rs:186-194`). */
    constexpr float luminance() const { return lightness; }

    /** @brief A copy of this color with the luminance, i.e. the lightness, set to the given value
     *  (Bevy `Luminance::with_luminance`, `bevy_color/src/hsla.rs:187-190`). */
    constexpr Hsla with_luminance(float lightness) const {
        Hsla result      = *this;
        result.lightness = lightness;
        return result;
    }

    /** @brief A darker version of this color, the lightness clamped to `[0.0, 1.0]` (Bevy
     *  `Luminance::darker`, `bevy_color/src/hsla.rs:196-201`). */
    constexpr Hsla darker(float amount) const {
        Hsla result      = *this;
        result.lightness = std::clamp(lightness - amount, 0.0f, 1.0f);
        return result;
    }

    /** @brief A lighter version of this color, the lightness capped at 1.0 — Bevy writes
     *  `(self.lightness + amount).min(1.)` here, with no lower clamp (Bevy `Luminance::lighter`,
     *  `bevy_color/src/hsla.rs:203-208`). */
    constexpr Hsla lighter(float amount) const {
        Hsla result      = *this;
        result.lightness = std::min(lightness + amount, 1.0f);
        return result;
    }

    /** @brief The components as an HSLA array (Bevy `ColorToComponents for Hsla`,
     *  `bevy_color/src/hsla.rs:211-214`). */
    constexpr std::array<float, 4> f32_array() const { return {hue, saturation, lightness, alpha}; }

    /** @brief The components as an HSL array (Bevy `bevy_color/src/hsla.rs:216-218`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {hue, saturation, lightness}; }

    /** @brief The components as a `glm::vec4` (Bevy `bevy_color/src/hsla.rs:220-222`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(hue, saturation, lightness, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `bevy_color/src/hsla.rs:224-226`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(hue, saturation, lightness); }

    /** @brief Build a color from an HSLA array (Bevy `bevy_color/src/hsla.rs:228-235`). */
    constexpr Hsla(const std::array<float, 4>& color)
        : hue(color[0]), saturation(color[1]), lightness(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an HSL array, with full alpha (Bevy
     *  `bevy_color/src/hsla.rs:237-244`). */
    constexpr Hsla(const std::array<float, 3>& color)
        : hue(color[0]), saturation(color[1]), lightness(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `bevy_color/src/hsla.rs:246-253`). */
    constexpr Hsla(const glm::vec4& color)
        : hue(color[0]), saturation(color[1]), lightness(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy
     *  `bevy_color/src/hsla.rs:255-262`). */
    constexpr Hsla(const glm::vec3& color)
        : hue(color[0]), saturation(color[1]), lightness(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Hsla` blocks over the file of the type being converted
    // from; the port defines every conversion that *produces* an `Hsla` here, so a reader asking
    // "how do I get a hue-saturation-lightness colour" has one place to look. The delegation order
    // is Bevy's own: the RGB-side spaces hop through `Hsva`, `Hwba` through `Hsva`, `Laba` through
    // `Xyza`, and `Oklcha` through `Oklaba`.

    /** @brief Bevy `impl From<Srgba> for Hsla` (`hsla.rs:321-325`): hops through `Hsva`. */
    Hsla(const Srgba& value);

    /** @brief Bevy `impl From<Xyza> for Hsla` (`hsla.rs:357-361`): hops through `Hsva`. */
    Hsla(const Xyza& value);

    /** @brief Bevy `impl From<LinearRgba> for Hsla` (`hsla.rs:333-337`): hops through `Hsva`. */
    Hsla(const LinearRgba& value);

    /** @brief Bevy `impl From<Hsva> for Hsla` (`hsla.rs:286-305`): the HSV → HSL formulas. */
    Hsla(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Hsla` (`hsla.rs:309-313`): hops through `Hsva`. */
    Hsla(const Hwba& value);

    /** @brief Bevy `impl From<Laba> for Hsla` (`laba.rs:323-327`): hops through `Xyza`. */
    Hsla(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for Hsla` (`hsla.rs:345-349`): hops through `Hsva`. */
    Hsla(const Lcha& value);

    /** @brief Bevy `impl From<Oklaba> for Hsla` (`oklaba.rs:286-290`): hops through
     *  `LinearRgba`. */
    Hsla(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Hsla` (`oklcha.rs:301-305`): hops through `Oklaba`. */
    Hsla(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Hsla` (`color.rs:565-580`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Hsla(const Color& value);
};

}  // namespace epix::color

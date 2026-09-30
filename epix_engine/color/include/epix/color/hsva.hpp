#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <epix/color/color_ops.hpp>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::color {

/** @brief Color in Hue-Saturation-Value (HSV) color space with alpha (Bevy `Hsva`,
 *  `bevy_color/src/hsva.rs:26-35`). Further information on this color model can be found on
 *  [Wikipedia](https://en.wikipedia.org/wiki/HSL_and_HSV).
 *
 *  Fields keep Bevy's names and ranges. Rust's derived `Default` is overridden to return
 *  `Self::new(0., 0., 1., 1.)` (`hsva.rs:79-83`), a pure white, which is what the member
 *  initialisers below encode. Bevy never calls `impl_componentwise_vector_space!` for this space
 *  (`bevy_color/src/lib.rs:178-277`), so unlike `LinearRgba` there is no componentwise arithmetic
 *  and no `ZERO`; the `color_ops.rs` traits are ordinary members with Bevy's names, as
 *  `epix/color/color_ops.hpp` documents, and Rust's `PartialEq` derive is the defaulted
 *  `operator==`. `Reflect`, serde and the crate-internal `StandardColor` marker trait are omitted,
 *  as throughout this module (`epix/color.hpp`). */
EPIX_EXPORT struct EPIX_API Hsva {
    /** @brief The hue channel. [0.0, 360.0] */
    float hue = 0.0f;
    /** @brief The saturation channel. [0.0, 1.0] */
    float saturation = 0.0f;
    /** @brief The value channel. [0.0, 1.0] */
    float value = 1.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Hsva&) const = default;

    constexpr Hsva() = default;

    /** @brief Construct a new `Hsva` color from components (Bevy `Hsva::new`,
     *  `bevy_color/src/hsva.rs:39-55`). */
    constexpr Hsva(float hue, float saturation, float value, float alpha)
        : hue(hue), saturation(saturation), value(value), alpha(alpha) {}

    /** @brief Construct a new `Hsva` color from (h, s, v) components, with the default alpha (1.0)
     *  (Bevy `Hsva::hsv`, `bevy_color/src/hsva.rs:57-66`). */
    constexpr static Hsva hsv(float hue, float saturation, float value) {
        return Hsva(hue, saturation, value, 1.0f);
    }

    /** @brief Return a copy of this color with the saturation channel set to the given value; both
     *  the inherent `Hsva::with_saturation` and `Saturation::with_saturation`, which Bevy writes
     *  with this same body (`bevy_color/src/hsva.rs:68-71` and `:137-144`). */
    constexpr Hsva with_saturation(float saturation) const {
        Hsva result       = *this;
        result.saturation = saturation;
        return result;
    }

    /** @brief Return a copy of this color with the value channel set to the given value (Bevy
     *  `Hsva::with_value`, `bevy_color/src/hsva.rs:73-76`). */
    constexpr Hsva with_value(float value) const {
        Hsva result  = *this;
        result.value = value;
        return result;
    }

    /** @brief A pure black color with full alpha, the `Gray::BLACK` associated constant (Bevy
     *  `Gray for Hsva`, `bevy_color/src/hsva.rs:98-101`). */
    static const Hsva BLACK;

    /** @brief A pure white color with full alpha, the `Gray::WHITE` associated constant (Bevy
     *  `Gray for Hsva`, `bevy_color/src/hsva.rs:98-101`). */
    static const Hsva WHITE;

    /** @brief Linearly interpolate between this color and another, the hue along the shortest arc
     *  (Bevy `Mix for Hsva`, `bevy_color/src/hsva.rs:85-96`). */
    Hsva mix(const Hsva& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Hsva& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Hsva gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`,
     *  `bevy_color/src/hsva.rs:103-107`). */
    constexpr Hsva with_alpha(float alpha) const {
        Hsva result  = *this;
        result.alpha = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, `bevy_color/src/hsva.rs:109-112`; spelled
     *  `get_alpha` because the field keeps that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`,
     *  `bevy_color/src/hsva.rs:114-117`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief A copy of this color with the given hue (Bevy `Hue::with_hue`,
     *  `bevy_color/src/hsva.rs:120-124`). */
    constexpr Hsva with_hue(float hue) const {
        Hsva result = *this;
        result.hue  = hue;
        return result;
    }

    /** @brief The color's hue in `[0.0, 360.0]` (Bevy `Hue::hue`,
     *  `bevy_color/src/hsva.rs:126-129`; spelled `get_hue` because the field keeps that name). */
    constexpr float get_hue() const { return hue; }

    /** @brief Set the color's hue (Bevy `Hue::set_hue`, `bevy_color/src/hsva.rs:131-134`). */
    constexpr void set_hue(float hue) { this->hue = hue; }

    /** @brief A copy of this color with the hue rotated by the given degrees (Bevy
     *  `Hue::rotate_hue`, `color_ops.rs:106-109`). */
    Hsva rotate_hue(float degrees) const { return with_hue(rem_euclid(get_hue() + degrees, 360.0f)); }

    /** @brief The color's saturation in `[0.0, 1.0]` (Bevy `Saturation::saturation`,
     *  `bevy_color/src/hsva.rs:146-149`; spelled `get_saturation` because the field keeps that
     *  name). */
    constexpr float get_saturation() const { return saturation; }

    /** @brief Set the color's saturation (Bevy `Saturation::set_saturation`,
     *  `bevy_color/src/hsva.rs:151-154`). */
    constexpr void set_saturation(float saturation) { this->saturation = saturation; }

    /** @brief The components as an HSVA array (Bevy `ColorToComponents for Hsva`,
     *  `bevy_color/src/hsva.rs:195-198`). */
    constexpr std::array<float, 4> f32_array() const { return {hue, saturation, value, alpha}; }

    /** @brief The components as an HSV array (Bevy `bevy_color/src/hsva.rs:200-202`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {hue, saturation, value}; }

    /** @brief The components as a `glm::vec4` (Bevy `bevy_color/src/hsva.rs:204-206`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(hue, saturation, value, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `bevy_color/src/hsva.rs:208-210`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(hue, saturation, value); }

    /** @brief Build a color from an HSVA array (Bevy `bevy_color/src/hsva.rs:212-219`). */
    constexpr Hsva(const std::array<float, 4>& color)
        : hue(color[0]), saturation(color[1]), value(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an HSV array, with full alpha (Bevy
     *  `bevy_color/src/hsva.rs:221-228`). */
    constexpr Hsva(const std::array<float, 3>& color)
        : hue(color[0]), saturation(color[1]), value(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `bevy_color/src/hsva.rs:230-237`). */
    constexpr Hsva(const glm::vec4& color)
        : hue(color[0]), saturation(color[1]), value(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy
     *  `bevy_color/src/hsva.rs:239-246`). */
    constexpr Hsva(const glm::vec3& color)
        : hue(color[0]), saturation(color[1]), value(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Hsva` blocks over the file of the type being converted
    // from; the port defines every conversion that *produces* an `Hsva` here, so a reader asking
    // "how do I get a hue-saturation-value colour" has one place to look. The delegation order is
    // Bevy's own: the RGB-side spaces hop through `Hwba`, `Hsla` converts directly, `Laba` through
    // `Xyza`, and `Oklcha` through `Oklaba`.

    /** @brief Bevy `impl From<Srgba> for Hsva` (`hsva.rs:251-255`): hops through `Hwba`. */
    Hsva(const Srgba& value);

    /** @brief Bevy `impl From<Xyza> for Hsva` (`hsva.rs:287-291`): hops through `Hwba`. */
    Hsva(const Xyza& value);

    /** @brief Bevy `impl From<LinearRgba> for Hsva` (`hsva.rs:263-267`): hops through `Hwba`. */
    Hsva(const LinearRgba& value);

    /** @brief Bevy `impl From<Hsla> for Hsva` (`hsla.rs:265-284`): the HSL → HSV formulas. */
    Hsva(const Hsla& value);

    /** @brief Bevy `impl From<Hwba> for Hsva` (`hsva.rs:174-193`): the HWB → HSV formulas. */
    Hsva(const Hwba& value);

    /** @brief Bevy `impl From<Laba> for Hsva` (`laba.rs:335-339`): hops through `Xyza`. */
    Hsva(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for Hsva` (`hsva.rs:275-279`): hops through `Hwba`. */
    Hsva(const Lcha& value);

    /** @brief Bevy `impl From<Oklaba> for Hsva` (`oklaba.rs:298-302`): hops through
     *  `LinearRgba`. */
    Hsva(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Hsva` (`oklcha.rs:313-317`): hops through `Oklaba`. */
    Hsva(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Hsva` (`color.rs:582-597`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Hsva(const Color& value);
};

}  // namespace epix::color

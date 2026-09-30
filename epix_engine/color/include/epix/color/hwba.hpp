#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <epix/color/color_ops.hpp>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::color {

/** @brief Color in Hue-Whiteness-Blackness (HWB) color space with alpha (Bevy `Hwba`,
 *  `bevy_color/src/hwba.rs:29-38`). Further information on this color model can be found on
 *  [Wikipedia](https://en.wikipedia.org/wiki/HWB_color_model).
 *
 *  Fields keep Bevy's names and ranges. Rust's derived `Default` is overridden to return
 *  `Self::new(0., 0., 1., 1.)` (`hwba.rs:82-86`), a pure black, which is what the member
 *  initialisers below encode. Bevy never calls `impl_componentwise_vector_space!` for this space
 *  (`bevy_color/src/lib.rs:178-277`), so unlike `LinearRgba` there is no componentwise arithmetic
 *  and no `ZERO`; the `color_ops.rs` traits are ordinary members with Bevy's names, as
 *  `epix/color/color_ops.hpp` documents, and Rust's `PartialEq` derive is the defaulted
 *  `operator==`. `Reflect`, serde and the crate-internal `StandardColor` marker trait are omitted,
 *  as throughout this module (`epix/color.hpp`). */
EPIX_EXPORT struct EPIX_API Hwba {
    /** @brief The hue channel. [0.0, 360.0] */
    float hue = 0.0f;
    /** @brief The whiteness channel. [0.0, 1.0] */
    float whiteness = 0.0f;
    /** @brief The blackness channel. [0.0, 1.0] */
    float blackness = 1.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Hwba&) const = default;

    constexpr Hwba() = default;

    /** @brief Construct a new `Hwba` color from components (Bevy `Hwba::new`,
     *  `bevy_color/src/hwba.rs:43-58`). */
    constexpr Hwba(float hue, float whiteness, float blackness, float alpha)
        : hue(hue), whiteness(whiteness), blackness(blackness), alpha(alpha) {}

    /** @brief Construct a new `Hwba` color from (h, w, b) components, with the default alpha
     *  (1.0) (Bevy `Hwba::hwb`, `bevy_color/src/hwba.rs:60-69`). */
    constexpr static Hwba hwb(float hue, float whiteness, float blackness) {
        return Hwba(hue, whiteness, blackness, 1.0f);
    }

    /** @brief Return a copy of this color with the whiteness channel set to the given value (Bevy
     *  `Hwba::with_whiteness`, `bevy_color/src/hwba.rs:71-74`). */
    constexpr Hwba with_whiteness(float whiteness) const {
        Hwba result      = *this;
        result.whiteness = whiteness;
        return result;
    }

    /** @brief Return a copy of this color with the blackness channel set to the given value (Bevy
     *  `Hwba::with_blackness`, `bevy_color/src/hwba.rs:76-79`). */
    constexpr Hwba with_blackness(float blackness) const {
        Hwba result      = *this;
        result.blackness = blackness;
        return result;
    }

    /** @brief A pure black color with full alpha, the `Gray::BLACK` associated constant (Bevy
     *  `Gray for Hwba`, `bevy_color/src/hwba.rs:101-104`). */
    static const Hwba BLACK;

    /** @brief A pure white color with full alpha, the `Gray::WHITE` associated constant (Bevy
     *  `Gray for Hwba`, `bevy_color/src/hwba.rs:101-104`). */
    static const Hwba WHITE;

    /** @brief Linearly interpolate between this color and another, the hue along the shortest arc
     *  (Bevy `Mix for Hwba`, `bevy_color/src/hwba.rs:88-99`). */
    Hwba mix(const Hwba& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Hwba& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Hwba gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`,
     *  `bevy_color/src/hwba.rs:106-110`). */
    constexpr Hwba with_alpha(float alpha) const {
        Hwba result  = *this;
        result.alpha = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, `bevy_color/src/hwba.rs:112-115`; spelled
     *  `get_alpha` because the field keeps that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`,
     *  `bevy_color/src/hwba.rs:117-120`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief A copy of this color with the given hue (Bevy `Hue::with_hue`,
     *  `bevy_color/src/hwba.rs:123-127`). */
    constexpr Hwba with_hue(float hue) const {
        Hwba result = *this;
        result.hue  = hue;
        return result;
    }

    /** @brief The color's hue in `[0.0, 360.0]` (Bevy `Hue::hue`,
     *  `bevy_color/src/hwba.rs:129-132`; spelled `get_hue` because the field keeps that name). */
    constexpr float get_hue() const { return hue; }

    /** @brief Set the color's hue (Bevy `Hue::set_hue`, `bevy_color/src/hwba.rs:134-137`). */
    constexpr void set_hue(float hue) { this->hue = hue; }

    /** @brief A copy of this color with the hue rotated by the given degrees (Bevy
     *  `Hue::rotate_hue`, `color_ops.rs:106-109`). */
    Hwba rotate_hue(float degrees) const { return with_hue(rem_euclid(get_hue() + degrees, 360.0f)); }

    /** @brief The components as an HWBA array (Bevy `ColorToComponents for Hwba`,
     *  `bevy_color/src/hwba.rs:140-143`). */
    constexpr std::array<float, 4> f32_array() const { return {hue, whiteness, blackness, alpha}; }

    /** @brief The components as an HWB array (Bevy `bevy_color/src/hwba.rs:145-147`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {hue, whiteness, blackness}; }

    /** @brief The components as a `glm::vec4` (Bevy `bevy_color/src/hwba.rs:149-151`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(hue, whiteness, blackness, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `bevy_color/src/hwba.rs:153-155`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(hue, whiteness, blackness); }

    /** @brief Build a color from an HWBA array (Bevy `bevy_color/src/hwba.rs:157-164`). */
    constexpr Hwba(const std::array<float, 4>& color)
        : hue(color[0]), whiteness(color[1]), blackness(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an HWB array, with full alpha (Bevy
     *  `bevy_color/src/hwba.rs:166-173`). */
    constexpr Hwba(const std::array<float, 3>& color)
        : hue(color[0]), whiteness(color[1]), blackness(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `bevy_color/src/hwba.rs:175-182`). */
    constexpr Hwba(const glm::vec4& color)
        : hue(color[0]), whiteness(color[1]), blackness(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy
     *  `bevy_color/src/hwba.rs:184-191`). */
    constexpr Hwba(const glm::vec3& color)
        : hue(color[0]), whiteness(color[1]), blackness(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Hwba` blocks over the file of the type being converted
    // from; the port defines every conversion that *produces* an `Hwba` here, so a reader asking
    // "how do I get a hue-whiteness-blackness colour" has one place to look. The delegation order
    // is Bevy's own: the RGB-side spaces hop through `Srgba`, `Hsla` through `Hsva`, `Laba` through
    // `Xyza` and `Oklcha` through `Oklaba`.

    /** @brief Bevy `impl From<Srgba> for Hwba` (`hwba.rs:194-230`): the HWB model from
     *  Appendix B of "HWB - A More Intuitive Hue-Based Color Model". */
    Hwba(const Srgba& value);

    /** @brief Bevy `impl From<Xyza> for Hwba` (`hwba.rs:295-299`): hops through `Srgba`. */
    Hwba(const Xyza& value);

    /** @brief Bevy `impl From<LinearRgba> for Hwba` (`hwba.rs:271-275`): hops through `Srgba`. */
    Hwba(const LinearRgba& value);

    /** @brief Bevy `impl From<Hsla> for Hwba` (`hsla.rs:315-319`): hops through `Hsva`. */
    Hwba(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Hwba` (`hsva.rs:157-172`): the HSV → HWB formulas. */
    Hwba(const Hsva& value);

    /** @brief Bevy `impl From<Laba> for Hwba` (`laba.rs:347-351`): hops through `Xyza`. */
    Hwba(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for Hwba` (`hwba.rs:283-287`): hops through `Srgba`. */
    Hwba(const Lcha& value);

    /** @brief Bevy `impl From<Oklaba> for Hwba` (`oklaba.rs:310-314`): hops through
     *  `LinearRgba`. */
    Hwba(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Hwba` (`oklcha.rs:325-329`): hops through `Oklaba`. */
    Hwba(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Hwba` (`color.rs:599-614`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Hwba(const Color& value);
};

}  // namespace epix::color

#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <epix/color/color_ops.hpp>
#include <epix/common.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::color {

/** @brief [CIE 1931](https://en.wikipedia.org/wiki/CIE_1931_color_space) color space, also known as
 *  XYZ, with an alpha channel (Bevy `Xyza`, `bevy_color/src/xyza.rs:9-34`).
 *
 *  Fields keep Bevy's names and `[0.0, 1.0]` meaning. Rust's derived `Default` is overridden to
 *  return `new(0., 0., 0., 1.)` (`xyza.rs:88-92`), which C++ encodes as the member initialisers
 *  below. `impl_componentwise_vector_space!` (`xyza.rs:38`) is expanded by
 *  `EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE`; the `color_ops.rs` traits are the members documented on
 *  that macro, and Rust's `PartialEq` derive is the defaulted `operator==`.
 *
 *  Bevy implements neither `EuclideanDistance` (`color_difference.rs`) nor `ColorToPacked` for
 *  `Xyza`, so this type has no `distance`/`distance_squared` and no `u8` packing. Also omitted, as
 *  throughout the port: `#[derive(Reflect)]`/`#[reflect(..)]`, serde, `Debug`, the GPU-side
 *  `Pod`/`Zeroable` impls and the crate-internal `StandardColor` marker trait (`lib.rs:148-176`). */
EPIX_EXPORT struct EPIX_API Xyza {
    /** @brief The x-axis. [0.0, 1.0] */
    float x = 0.0f;
    /** @brief The y-axis, intended to represent luminance. [0.0, 1.0] */
    float y = 0.0f;
    /** @brief The z-axis. [0.0, 1.0] */
    float z = 0.0f;
    /** @brief The alpha channel. [0.0, 1.0] */
    float alpha = 1.0f;

    /** @brief Bevy derives `Debug, Clone, Copy, PartialEq`; the port leaves copying implicit and
     *  spells the equality. */
    constexpr bool operator==(const Xyza&) const = default;

    constexpr Xyza() = default;

    /** @brief Construct a color from components (Bevy `Xyza::new`, `xyza.rs:49-51`). */
    constexpr Xyza(float x, float y, float z, float alpha) : x(x), y(y), z(z), alpha(alpha) {}

    EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE(Xyza, x, y, z, alpha)

    /** @brief A fully black color with full alpha (Bevy `Xyza::BLACK`, `xyza.rs:153`). */
    static const Xyza BLACK;

    /** @brief The XYZ color of linear white; the `Gray::WHITE` of this space (Bevy `Xyza::WHITE`,
     *  `xyza.rs:154`), which is the same color as `D65_WHITE`. */
    static const Xyza WHITE;

    /** @brief [D65 White Point](https://en.wikipedia.org/wiki/Illuminant_D65#Definition) (Bevy
     *  `Xyza::D65_WHITE`, `xyza.rs:84-85`). `Laba` and `Lcha` define themselves relative to it, so
     *  every XYZ ↔ Lab conversion divides and multiplies by it. */
    static const Xyza D65_WHITE;

    /** @brief Construct a color from (x, y, z) components, with the default alpha (1.0) (Bevy
     *  `Xyza::xyz`, `xyza.rs:60-67`). */
    constexpr static Xyza xyz(float x, float y, float z) { return Xyza(x, y, z, 1.0f); }

    /** @brief Return a copy of this color with the x-axis set to the given value (Bevy
     *  `Xyza::with_x`, `xyza.rs:70-72`). */
    constexpr Xyza with_x(float x) const {
        Xyza result = *this;
        result.x    = x;
        return result;
    }

    /** @brief Return a copy of this color with the y-axis set to the given value (Bevy
     *  `Xyza::with_y`, `xyza.rs:75-77`). */
    constexpr Xyza with_y(float y) const {
        Xyza result = *this;
        result.y    = y;
        return result;
    }

    /** @brief Return a copy of this color with the z-axis set to the given value (Bevy
     *  `Xyza::with_z`, `xyza.rs:80-82`). */
    constexpr Xyza with_z(float z) const {
        Xyza result = *this;
        result.z    = z;
        return result;
    }

    /** @brief The luminance, which in this space *is* the y-axis (Bevy `Luminance for Xyza`,
     *  `xyza.rs:120-122`), unlike `LinearRgba`, whose luminance is the rounded CIE weighted sum. */
    constexpr float luminance() const { return y; }

    /** @brief A copy of this color with the luminance (the y-axis) set to the given value (Bevy
     *  `Xyza::with_luminance`, `xyza.rs:112-118`). */
    constexpr Xyza with_luminance(float lightness) const {
        Xyza result = *this;
        result.y    = lightness;
        return result;
    }

    /** @brief Make the color darker by some amount, clamping the y-axis into `[0.0, 1.0]` (Bevy
     *  `Xyza::darker`, `xyza.rs:124-129`). */
    constexpr Xyza darker(float amount) const { return with_luminance(std::clamp(y - amount, 0.0f, 1.0f)); }

    /** @brief Make the color lighter by some amount, capping the y-axis at 1.0 (Bevy
     *  `Xyza::lighter`, `xyza.rs:131-136`). Note that Bevy uses `min(1.0)` here and `clamp(0.0, 1.0)`
     *  in `darker`, just as `LinearRgba` does. */
    constexpr Xyza lighter(float amount) const { return with_luminance(std::min(y + amount, 1.0f)); }

    /** @brief Linearly interpolate between this color and another, componentwise (Bevy `Mix for
     *  Xyza`, `xyza.rs:139-150`). */
    Xyza mix(const Xyza& other, float factor) const;

    /** @brief The provided `Mix::mix_assign` (`color_ops.rs:33-43`). */
    void mix_assign(const Xyza& other, float factor) { *this = mix(other, factor); }

    /** @brief The provided `Gray::gray` (`color_ops.rs:46-56`); the space's `BLACK` and `WHITE`
     *  are the two constants above, as Bevy's associated constants. */
    static Xyza gray(float lightness) { return BLACK.mix(WHITE, lightness); }

    /** @brief A copy of this color with the given alpha (Bevy `Alpha::with_alpha`, `xyza.rs:95-98`). */
    constexpr Xyza with_alpha(float alpha) const {
        Xyza result  = *this;
        result.alpha = alpha;
        return result;
    }

    /** @brief The color's alpha (Bevy `Alpha::alpha`, `xyza.rs:100-103`, spelled `get_alpha`
     *  because the field keeps that name). */
    constexpr float get_alpha() const { return alpha; }

    /** @brief Set the color's alpha (Bevy `Alpha::set_alpha`, `xyza.rs:105-108`). */
    constexpr void set_alpha(float alpha) { this->alpha = alpha; }

    /** @brief The provided `Alpha::is_fully_transparent` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_transparent() const { return get_alpha() <= 0.0f; }

    /** @brief The provided `Alpha::is_fully_opaque` (`color_ops.rs:59-78`). */
    constexpr bool is_fully_opaque() const { return get_alpha() >= 1.0f; }

    /** @brief The components as an XYZA array (Bevy `ColorToComponents::to_f32_array`,
     *  `xyza.rs:158-160`; the port drops the `to_` prefix, so the constructors below and these
     *  accessors are the two halves of the trait). */
    constexpr std::array<float, 4> f32_array() const { return {x, y, z, alpha}; }

    /** @brief The components as an XYZ array (Bevy `ColorToComponents::to_f32_array_no_alpha`,
     *  `xyza.rs:162-164`). */
    constexpr std::array<float, 3> f32_array_no_alpha() const { return {x, y, z}; }

    /** @brief The components as a `glm::vec4` (Bevy `ColorToComponents::to_vec4`,
     *  `xyza.rs:166-168`). */
    constexpr glm::vec4 vec4() const { return glm::vec4(x, y, z, alpha); }

    /** @brief The components as a `glm::vec3` (Bevy `ColorToComponents::to_vec3`,
     *  `xyza.rs:170-172`). */
    constexpr glm::vec3 vec3() const { return glm::vec3(x, y, z); }

    /** @brief Build a color from an XYZA array (Bevy `Xyza::from_f32_array`, `xyza.rs:174-181`). */
    constexpr Xyza(const std::array<float, 4>& color)
        : x(color[0]), y(color[1]), z(color[2]), alpha(color[3]) {}

    /** @brief Build a color from an XYZ array, with full alpha (Bevy
     *  `Xyza::from_f32_array_no_alpha`, `xyza.rs:183-190`). */
    constexpr Xyza(const std::array<float, 3>& color) : x(color[0]), y(color[1]), z(color[2]), alpha(1.0f) {}

    /** @brief Build a color from a `glm::vec4` (Bevy `Xyza::from_vec4`, `xyza.rs:192-199`). */
    constexpr Xyza(const glm::vec4& color) : x(color[0]), y(color[1]), z(color[2]), alpha(color[3]) {}

    /** @brief Build a color from a `glm::vec3`, with full alpha (Bevy `Xyza::from_vec3`,
     *  `xyza.rs:201-208`). */
    constexpr Xyza(const glm::vec3& color) : x(color[0]), y(color[1]), z(color[2]), alpha(1.0f) {}

    // Bevy spreads the ten `impl From<A> for Xyza` blocks over the file of the type being converted
    // from; the port defines every conversion that *produces* an `Xyza` here, as this type's
    // converting constructors, so a reader asking "how do I get an XYZ colour" has one place to
    // look. The delegation order is Bevy's own: `Srgba` and `Oklaba` hop through `LinearRgba`, the
    // hue-based spaces through `Hwba`, and `Laba` finally through the CIE formula below, which is the
    // only conversion in this file with a body of its own.

    /** @brief Bevy `impl From<LinearRgba> for Xyza` (`xyza.rs:211-233`): the linear sRGB → XYZ
     *  matrix, alpha copied. */
    Xyza(const LinearRgba& value);

    /** @brief Bevy `impl From<Srgba> for Xyza` (`srgba.rs:424-428`): hops through `LinearRgba`,
     *  i.e. the sRGB transfer function first. */
    Xyza(const Srgba& value);

    /** @brief Bevy `impl From<Hsla> for Xyza` (`hsla.rs:363-367`): hops through `Hsva`. */
    Xyza(const Hsla& value);

    /** @brief Bevy `impl From<Hsva> for Xyza` (`hsva.rs:293-297`): hops through `Hwba`. */
    Xyza(const Hsva& value);

    /** @brief Bevy `impl From<Hwba> for Xyza` (`hwba.rs:301-305`): hops through `Srgba`. */
    Xyza(const Hwba& value);

    /** @brief Bevy `impl From<Laba> for Xyza` (`laba.rs:214-260`): the CIE Lab → XYZ formula, with
     *  Bevy's `CIE_EPSILON`/`CIE_KAPPA` branch on every one of `fx`/`fy`/`fz`. */
    Xyza(const Laba& value);

    /** @brief Bevy `impl From<Lcha> for Xyza` (`lcha.rs:332-336`): hops through `Laba`. */
    Xyza(const Lcha& value);

    /** @brief Bevy `impl From<Oklaba> for Xyza` (`oklaba.rs:346-350`): hops through `LinearRgba`. */
    Xyza(const Oklaba& value);

    /** @brief Bevy `impl From<Oklcha> for Xyza` (`oklcha.rs:385-389`): hops through `Oklaba`. */
    Xyza(const Oklcha& value);

    /** @brief Bevy `impl From<Color> for Xyza` (`color.rs:684-699`): the variant's own value is
     *  returned unchanged, every other variant converts. */
    Xyza(const Color& value);
};

}  // namespace epix::color

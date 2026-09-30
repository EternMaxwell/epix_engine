#pragma once

#ifndef EPIX_CXX_MODULE
#include <cmath>
#include <cstdint>
#endif

namespace epix::color {

// Forward declarations of the crate's types, so every space header can name its siblings and
// `Color` in by-value conversion signatures without including them: Bevy's `impl From<A> for B`
// blocks are spread over one file per space, so the body of a conversion usually lives in the
// file of the type it converts *to*.
//
// `impl From<A> for B` becomes `B`'s converting constructor `B(const A&)` - declared in `B`'s
// header and defined in `B`'s `.cpp` - so a space owns the conversions that *produce* it, and
// `B::from(a)` reads `B(a)` or `static_cast<B>(a)` at the call site.
//
// The tag is `struct`, matching each type's definition: MSVC mangles a class-type parameter with
// the class-key it saw *first* (`U` for `struct`, `V` for `class`), so a `class X;` here and a
// `struct X` definition later would mangle the same overload two ways across translation units and
// the link would fail with `error LNK2019: unresolved external symbol` for every conversion.
struct Srgba;
struct LinearRgba;
struct Hsla;
struct Hsva;
struct Hwba;
struct Laba;
struct Lcha;
struct Oklaba;
struct Oklcha;
struct Xyza;
struct Color;

/** @brief `f32::rem_euclid` (Bevy spells it `f32::rem_euclid` in `bevy_color/src/color_ops.rs`).
 *  Rust's `%` keeps the sign of the dividend, so the negative case adds `rhs.abs()`. */
inline float rem_euclid(float self, float rhs) {
    const float remainder = std::fmod(self, rhs);
    return remainder < 0.0f ? remainder + std::fabs(rhs) : remainder;
}

/** @brief Hue interpolation along the shortest arc (Bevy `lerp_hue`,
 *  `bevy_color/src/color_ops.rs:162-165`). Used by every `Mix` of a hue-based space. */
inline float lerp_hue(float a, float b, float t) {
    return rem_euclid(a + (rem_euclid(b - a + 180.0f, 360.0f) - 180.0f) * t, 360.0f);
}

/** @brief Expands Bevy's `impl_componentwise_vector_space!` (`bevy_color/src/lib.rs:178-277`)
 *  for one colour space with four fields, passed in declaration order.
 *
 *  Bevy's macro gives each space the componentwise `Add`, `AddAssign`, `Neg`, `Sub`, `SubAssign`,
 *  `Mul<f32>`, `Mul<Self> for f32`, `MulAssign<f32>`, `Div<f32>` and `DivAssign<f32>` operators,
 *  the `VectorSpace::ZERO` constant, and `StableInterpolate::interpolate_stable`, which Bevy
 *  defines as `VectorSpace::lerp(*self, *other, t)`, i.e. `self + (other - self) * t`.
 *
 *  `Zeroable`/`Pod` have no C++ counterpart: the engine's GPU-side spelling of a colour is the
 *  generic pod `render::render_resource::ShaderTypeInfo<glm::vec4>`.
 *
 *  `ZERO` is only declared here: a class cannot hold an in-class `constexpr` member of its own
 *  still-incomplete type, so each space that expands this macro defines it out of line with
 *  `EPIX_COLOR_DEFINE_ZERO` below, the form the rest of the port uses for a Bevy associated
 *  constant (`LinearRgba::NaN`, `Anchor::CENTER`, `MeshPipelineKey::NONE`).
 *
 *  @param TYPE the colour space type.
 *  @param A @param B @param C @param D the four field names, in declaration order. */
#define EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE(TYPE, A, B, C, D)                                              \
    constexpr TYPE operator+(const TYPE& rhs) const {                                                        \
        return TYPE{A + rhs.A, B + rhs.B, C + rhs.C, D + rhs.D};                                             \
    }                                                                                                        \
    constexpr TYPE& operator+=(const TYPE& rhs) {                                                            \
        *this = *this + rhs;                                                                                 \
        return *this;                                                                                        \
    }                                                                                                        \
    constexpr TYPE operator-() const { return TYPE{-A, -B, -C, -D}; }                                        \
    constexpr TYPE operator-(const TYPE& rhs) const {                                                        \
        return TYPE{A - rhs.A, B - rhs.B, C - rhs.C, D - rhs.D};                                             \
    }                                                                                                        \
    constexpr TYPE& operator-=(const TYPE& rhs) {                                                            \
        *this = *this - rhs;                                                                                 \
        return *this;                                                                                        \
    }                                                                                                        \
    constexpr TYPE operator*(float rhs) const {                                                              \
        return TYPE{A * rhs, B * rhs, C * rhs, D * rhs};                                                     \
    }                                                                                                        \
    friend constexpr TYPE operator*(float lhs, const TYPE& rhs) { return rhs * lhs; }                        \
    constexpr TYPE& operator*=(float rhs) {                                                                  \
        *this = *this * rhs;                                                                                 \
        return *this;                                                                                        \
    }                                                                                                        \
    constexpr TYPE operator/(float rhs) const {                                                              \
        return TYPE{A / rhs, B / rhs, C / rhs, D / rhs};                                                     \
    }                                                                                                        \
    constexpr TYPE& operator/=(float rhs) {                                                                  \
        *this = *this / rhs;                                                                                 \
        return *this;                                                                                        \
    }                                                                                                        \
    static const TYPE ZERO;                                                                                  \
    constexpr TYPE interpolate_stable(const TYPE& other, float t) const { return *this + (other - *this) * t; }

/** @brief Defines `ZERO` for a space that invoked `EPIX_COLOR_COMPONENTWISE_VECTOR_SPACE`.
 *
 *  Invoke this once per space that expands that macro, in the space's own `.cpp`, exactly as the
 *  rest of the port defines a Bevy associated constant out of line (Bevy `bevy_math::VectorSpace::ZERO`,
 *  `bevy_color/src/lib.rs:246-249`; the port's example is `BorderRect::ZERO` in
 *  `sprite/src/texture_slice.cpp:9`).
 *
 *  It cannot be invoked in the header: the colour spaces are `EPIX_API`, which is
 *  `__declspec(dllimport)` for every consumer of the shared library, and MSVC rejects the
 *  definition of a `dllimport` class's static data member in a header with
 *  `error C2491: definition of dllimport static data member not allowed`.
 *
 *  @param TYPE the colour space type. */
#define EPIX_COLOR_DEFINE_ZERO(TYPE) const TYPE TYPE::ZERO(0.0f, 0.0f, 0.0f, 0.0f);

/** @brief The contracts `color_ops.rs` gives every colour space, as members of the space itself.
 *
 *  `bevy_color/src/color_ops.rs` declares eight traits; the port spells each one's methods as
 *  members of the space with Bevy's names, so a call site reads the same in both languages:
 *
 *  - `Luminance` (`color_ops.rs:6-30`): `luminance`, `with_luminance`, `darker`, `lighter`.
 *  - `Mix` (`color_ops.rs:33-43`): `mix`, and `mix_assign`, whose Bevy body is
 *    `*self = self.mix(other, factor)`.
 *  - `Gray` (`color_ops.rs:46-56`): the space's own `BLACK`/`WHITE` constants plus the provided
 *    `gray(lightness) = BLACK.mix(&WHITE, lightness)`.
 *  - `Alpha` (`color_ops.rs:59-78`): `with_alpha`, `alpha`, `set_alpha`, and the provided
 *    `is_fully_transparent = alpha <= 0.0` / `is_fully_opaque = alpha >= 1.0`. The accessor is
 *    spelled `get_alpha` where the space keeps Bevy's `alpha` field, because C++ cannot give a
 *    member function the name of a data member.
 *  - `Hue` (`color_ops.rs:95-110`): `with_hue`, `hue`, `set_hue`, `rotate_hue`; again `get_hue`
 *    where the space has a `hue` field.
 *  - `Saturation` (`color_ops.rs:116-125`): `with_saturation`, `saturation`, `set_saturation`;
 *    spelled `get_saturation` where the space has a `saturation` field.
 *  - `ColorToComponents` (`color_ops.rs:128-145`): the accessors `f32_array`, `f32_array_no_alpha`,
 *    `vec4`, `vec3`, and the matching constructors from `std::array<float, 4>`,
 *    `std::array<float, 3>`, `glm::vec4` and `glm::vec3`. `[f32; 4]` is `std::array<float, 4>`,
 *    `bevy_math::Vec4` is `glm::vec4`; the `to_` prefix is dropped, as in the rest of the port.
 *  - `ColorToPacked` (`color_ops.rs:148-157`): the accessors `u8_array`, `u8_array_no_alpha` and
 *    the matching constructors from `std::array<std::uint8_t, 4>` / `std::array<std::uint8_t, 3>`.
 *  - `EuclideanDistance` (`color_difference.rs:7-15`): `distance_squared` and the provided
 *    `distance`, which Bevy defines as `ops::sqrt(distance_squared(other))` and which explicitly
 *    ignores alpha.
 *
 *  `Alpha` is also implemented for Rust's `f32` (`color_ops.rs:80-92`); C++ cannot add members to
 *  a builtin type, so that one implementation is these free functions. */
inline float with_alpha(float self, float) { return self; }

inline float alpha(float self) { return self; }

inline void set_alpha(float& self, float alpha) { self = alpha; }

inline bool is_fully_transparent(float self) { return self <= 0.0f; }

inline bool is_fully_opaque(float self) { return self >= 1.0f; }

}  // namespace epix::color

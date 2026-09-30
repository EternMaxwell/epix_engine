#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <epix/color.hpp>

using namespace epix::color;

// Ground truth for every assertion below is the `#[cfg(test)] mod tests` block at the end of
// tmp/bevy-0.18.0/crates/bevy_color/src/oklaba.rs and .../oklcha.rs.

TEST(Oklaba, DefaultsConstantsAndHelpers) {
    // Rust's `Default` (oklaba.rs:90-94) is `Oklaba::new(1., 0., 0., 1.)`.
    const Oklaba defaulted{};
    EXPECT_FLOAT_EQ(defaulted.lightness, 1.0f);
    EXPECT_FLOAT_EQ(defaulted.a, 0.0f);
    EXPECT_FLOAT_EQ(defaulted.b, 0.0f);
    EXPECT_FLOAT_EQ(defaulted.alpha, 1.0f);
    EXPECT_TRUE(defaulted == Oklaba(1.0f, 0.0f, 0.0f, 1.0f));

    // `Gray for Oklaba` (oklaba.rs:109-112).
    EXPECT_FLOAT_EQ(Oklaba::BLACK.lightness, 0.0f);
    EXPECT_FLOAT_EQ(Oklaba::BLACK.alpha, 1.0f);
    EXPECT_FLOAT_EQ(Oklaba::WHITE.lightness, 1.0f);
    EXPECT_FLOAT_EQ(Oklaba::WHITE.a, 0.0f);
    EXPECT_FLOAT_EQ(Oklaba::WHITE.b, 0.000000059604645f);

    // `Oklaba::lab` (oklaba.rs:65-72) defaults alpha to 1.0.
    const Oklaba lab = Oklaba::lab(0.4f, 0.1f, 0.2f);
    EXPECT_FLOAT_EQ(lab.alpha, 1.0f);
    EXPECT_FLOAT_EQ(lab.with_lightness(0.6f).lightness, 0.6f);
    EXPECT_FLOAT_EQ(lab.with_lightness(0.6f).a, 0.1f);
    EXPECT_FLOAT_EQ(lab.with_a(0.3f).a, 0.3f);
    EXPECT_FLOAT_EQ(lab.with_b(0.4f).b, 0.4f);

    // The provided `Gray::gray` is `BLACK.mix(&WHITE, lightness)` (color_ops.rs:46-56).
    EXPECT_TRUE(Oklaba::gray(0.0f) == Oklaba::BLACK);
    EXPECT_TRUE(Oklaba::gray(1.0f) == Oklaba::WHITE);
    const Oklaba mid_gray = Oklaba::gray(0.5f);
    EXPECT_NEAR(mid_gray.lightness, 0.5f, 1e-6f);
    EXPECT_NEAR(mid_gray.b, 0.000000059604645f * 0.5f, 1e-12f);
    EXPECT_FLOAT_EQ(mid_gray.alpha, 1.0f);
}

TEST(Oklaba, LuminanceAlphaAndDistance) {
    const Oklaba color = Oklaba::lab(0.2f, 0.1f, 0.2f);
    EXPECT_FLOAT_EQ(color.luminance(), 0.2f);
    EXPECT_FLOAT_EQ(color.with_luminance(0.75f).lightness, 0.75f);

    // `Luminance::darker`/`lighter` clamp to black and white (oklaba.rs:141-157).
    EXPECT_FLOAT_EQ(color.darker(0.5f).lightness, 0.0f);
    EXPECT_FLOAT_EQ(color.lighter(0.9f).lightness, 1.0f);
    EXPECT_FLOAT_EQ(color.darker(0.1f).lightness, 0.1f);
    EXPECT_FLOAT_EQ(color.lighter(0.1f).lightness, 0.3f);
    // The other channels are carried through untouched.
    EXPECT_FLOAT_EQ(color.darker(0.5f).a, 0.1f);
    EXPECT_FLOAT_EQ(color.darker(0.5f).b, 0.2f);
    EXPECT_FLOAT_EQ(color.darker(0.5f).alpha, 1.0f);

    // `Alpha for Oklaba` (oklaba.rs:114-129); the accessor is `get_alpha` because the field keeps
    // Bevy's name.
    EXPECT_FLOAT_EQ(color.get_alpha(), 1.0f);
    EXPECT_FLOAT_EQ(color.with_alpha(0.25f).get_alpha(), 0.25f);
    EXPECT_FALSE(color.with_alpha(0.5f).is_fully_opaque());
    EXPECT_TRUE(color.with_alpha(0.0f).is_fully_transparent());
    EXPECT_TRUE(color.is_fully_opaque());
    Oklaba mutated = color;
    mutated.set_alpha(0.0f);
    EXPECT_TRUE(mutated.is_fully_transparent());

    // `EuclideanDistance for Oklaba` (oklaba.rs:160-167) ignores alpha.
    const Oklaba black = Oklaba::lab(0.0f, 0.0f, 0.0f);
    const Oklaba white = Oklaba::lab(0.5f, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(white.distance_squared(black), 0.75f);
    EXPECT_NEAR(white.distance(black), std::sqrt(0.75f), 1e-5f);
    EXPECT_FLOAT_EQ(white.distance_squared(white.with_alpha(0.0f)), 0.0f);
}

TEST(Oklaba, FromLinearRgbaRoundTrip) {
    // Bevy's `test_to_from_linear` (oklaba.rs:390-399).
    const Oklaba oklaba = Oklaba(0.5f, 0.5f, 0.5f, 1.0f);
    const LinearRgba linear = LinearRgba(oklaba);
    const Oklaba round_tripped = Oklaba(linear);
    EXPECT_NEAR(round_tripped.lightness, oklaba.lightness, 1e-3f);
    EXPECT_NEAR(round_tripped.a, oklaba.a, 1e-3f);
    EXPECT_NEAR(round_tripped.b, oklaba.b, 1e-3f);
    EXPECT_NEAR(round_tripped.alpha, oklaba.alpha, 1e-3f);

    // A neutral linear color is achromatic in Oklab: the three matrix rows sum to one, so the
    // cube-rooted LMS triple is uniform and the a/b rows cancel (oklaba.rs:223-244).
    const Oklaba grey = Oklaba(LinearRgba(0.5f, 0.5f, 0.5f, 1.0f));
    EXPECT_NEAR(grey.lightness, 0.7937005f, 1e-5f);
    EXPECT_NEAR(grey.a, 0.0f, 1e-5f);
    EXPECT_NEAR(grey.b, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(grey.alpha, 1.0f);

    // Bevy's `test_to_from_srgba` (oklaba.rs:357-366) goes out through Srgba and back.
    const Srgba srgba = Srgba(oklaba);
    const Oklaba from_srgb = Oklaba(srgba);
    EXPECT_NEAR(from_srgb.lightness, oklaba.lightness, 1e-3f);
    EXPECT_NEAR(from_srgb.a, oklaba.a, 1e-3f);
    EXPECT_NEAR(from_srgb.b, oklaba.b, 1e-3f);
    EXPECT_NEAR(from_srgb.alpha, oklaba.alpha, 1e-3f);
}

TEST(Oklaba, ColorToComponentsRoundTrip) {
    const Oklaba color = Oklaba(0.25f, -0.5f, 0.75f, 0.5f);
    const Oklaba no_alpha = Oklaba(0.25f, -0.5f, 0.75f, 1.0f);

    const std::array<float, 4> array = color.f32_array();
    EXPECT_FLOAT_EQ(array[0], 0.25f);
    EXPECT_FLOAT_EQ(array[3], 0.5f);
    EXPECT_TRUE(Oklaba(array) == color);

    const std::array<float, 3> array_no_alpha = color.f32_array_no_alpha();
    EXPECT_FLOAT_EQ(array_no_alpha[2], 0.75f);
    EXPECT_TRUE(Oklaba(array_no_alpha) == no_alpha);

    EXPECT_TRUE(Oklaba(color.vec4()) == color);
    EXPECT_TRUE(Oklaba(color.vec3()) == no_alpha);
}

TEST(Oklcha, DefaultsConstantsAndHelpers) {
    // Rust's `Default` (oklcha.rs:107-111) is `Oklcha::new(1., 0., 0., 1.)`.
    const Oklcha defaulted{};
    EXPECT_FLOAT_EQ(defaulted.lightness, 1.0f);
    EXPECT_FLOAT_EQ(defaulted.chroma, 0.0f);
    EXPECT_FLOAT_EQ(defaulted.hue, 0.0f);
    EXPECT_FLOAT_EQ(defaulted.alpha, 1.0f);

    // `Gray for Oklcha` (oklcha.rs:126-129).
    EXPECT_FLOAT_EQ(Oklcha::BLACK.lightness, 0.0f);
    EXPECT_FLOAT_EQ(Oklcha::BLACK.hue, 0.0f);
    EXPECT_FLOAT_EQ(Oklcha::BLACK.alpha, 1.0f);
    EXPECT_FLOAT_EQ(Oklcha::WHITE.lightness, 1.0f);
    EXPECT_FLOAT_EQ(Oklcha::WHITE.chroma, 0.000000059604645f);
    EXPECT_FLOAT_EQ(Oklcha::WHITE.hue, 90.0f);

    // `Oklcha::lch` (oklcha.rs:63-65) defaults alpha to 1.0.
    const Oklcha lch = Oklcha::lch(0.5f, 0.25f, 30.0f);
    EXPECT_FLOAT_EQ(lch.alpha, 1.0f);
    EXPECT_FLOAT_EQ(lch.with_lightness(0.75f).lightness, 0.75f);
    EXPECT_FLOAT_EQ(lch.with_lightness(0.75f).chroma, 0.25f);
    EXPECT_FLOAT_EQ(lch.with_chroma(0.1f).chroma, 0.1f);
    EXPECT_FLOAT_EQ(lch.with_chroma(0.1f).hue, 30.0f);
}

TEST(Oklcha, OklabOklchPolarRoundTrip) {
    // At 45 degrees `a` and `b` are equal (oklcha.rs:275-291).
    const Oklaba polar = Oklaba(Oklcha::lch(0.5f, 0.5f, 45.0f));
    EXPECT_NEAR(polar.lightness, 0.5f, 1e-5f);
    EXPECT_NEAR(polar.a, 0.35355339f, 1e-5f);
    EXPECT_NEAR(polar.b, polar.a, 1e-6f);
    EXPECT_FLOAT_EQ(polar.alpha, 1.0f);

    const Oklcha back = Oklcha(polar);
    EXPECT_NEAR(back.lightness, 0.5f, 1e-5f);
    EXPECT_NEAR(back.chroma, 0.5f, 1e-5f);
    EXPECT_NEAR(back.hue, 45.0f, 1e-3f);

    // The one guard in the pair: a negative `atan2` result is wrapped into [0, 360)
    // (oklcha.rs:269).
    EXPECT_NEAR(Oklcha(Oklaba(0.5f, 0.5f, -0.5f, 1.0f)).hue, 315.0f, 1e-3f);
    EXPECT_NEAR(Oklcha(Oklaba(0.5f, -1.0f, 0.0f, 1.0f)).hue, 180.0f, 1e-3f);
    EXPECT_NEAR(Oklcha(Oklaba(0.5f, 0.0f, 1.0f, 1.0f)).hue, 90.0f, 1e-3f);

    // Chroma zero: `atan2(0, 0)` is 0.0 (not negative), so the hue stays 0 and the round trip
    // collapses back to the achromatic axis.
    const Oklcha achromatic = Oklcha(Oklaba(0.5f, 0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(achromatic.chroma, 0.0f);
    EXPECT_FLOAT_EQ(achromatic.hue, 0.0f);
    EXPECT_FLOAT_EQ(achromatic.lightness, 0.5f);
    EXPECT_NEAR(Oklaba(achromatic).a, 0.0f, 1e-7f);
    EXPECT_NEAR(Oklaba(achromatic).b, 0.0f, 1e-7f);
}

TEST(Oklcha, LuminanceAlphaHueAndDistance) {
    const Oklcha color = Oklcha::lch(0.4f, 0.2f, 120.0f);
    EXPECT_FLOAT_EQ(color.luminance(), 0.4f);
    EXPECT_FLOAT_EQ(color.with_luminance(0.9f).lightness, 0.9f);

    // `Luminance::darker`/`lighter` clamp (oklcha.rs:175-191), keeping chroma and hue.
    EXPECT_FLOAT_EQ(color.darker(0.5f).lightness, 0.0f);
    EXPECT_FLOAT_EQ(color.lighter(0.9f).lightness, 1.0f);
    EXPECT_FLOAT_EQ(color.darker(0.1f).lightness, 0.3f);
    EXPECT_FLOAT_EQ(color.darker(0.5f).chroma, 0.2f);
    EXPECT_FLOAT_EQ(color.darker(0.5f).hue, 120.0f);

    // `Hue for Oklcha` (oklcha.rs:148-163) plus the provided `rotate_hue`, which is
    // `rem_euclid(hue + degrees, 360.)` (color_ops.rs:106-109).
    EXPECT_FLOAT_EQ(color.get_hue(), 120.0f);
    EXPECT_FLOAT_EQ(color.with_hue(10.0f).hue, 10.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(300.0f).hue, 60.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(-130.0f).hue, 350.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(0.0f).hue, 120.0f);
    Oklcha mutated = color;
    mutated.set_hue(200.0f);
    EXPECT_FLOAT_EQ(mutated.hue, 200.0f);

    // `Alpha for Oklcha` (oklcha.rs:131-146).
    EXPECT_FLOAT_EQ(color.get_alpha(), 1.0f);
    EXPECT_FLOAT_EQ(color.with_alpha(0.5f).get_alpha(), 0.5f);
    EXPECT_FALSE(color.with_alpha(0.5f).is_fully_opaque());
    EXPECT_TRUE(color.with_alpha(0.0f).is_fully_transparent());

    // `EuclideanDistance for Oklcha` (oklcha.rs:194-201) measures the raw hue difference, so the
    // hue channel is a plain distance component and alpha is ignored.
    EXPECT_FLOAT_EQ(Oklcha::lch(0.0f, 0.0f, 0.0f).distance_squared(Oklcha::lch(0.0f, 0.0f, 10.0f)),
                    100.0f);
    EXPECT_FLOAT_EQ(color.distance_squared(color.with_alpha(0.0f)), 0.0f);
}

TEST(Oklcha, MixAlongTheShortestArcAndSequentialDispersed) {
    // `Mix for Oklcha` (oklcha.rs:113-124) interpolates the hue with `lerp_hue`
    // (color_ops.rs:33-37), i.e. along the shortest arc: 350 -> 10 wraps through 360, not through
    // 180.
    const Oklcha a = Oklcha::lch(0.5f, 0.1f, 350.0f);
    const Oklcha b = Oklcha::lch(0.5f, 0.1f, 10.0f);
    const Oklcha mid = a.mix(b, 0.5f);
    EXPECT_NEAR(mid.hue, 0.0f, 1e-5f);
    EXPECT_NEAR(mid.lightness, 0.5f, 1e-6f);
    EXPECT_NEAR(mid.chroma, 0.1f, 1e-6f);
    EXPECT_FLOAT_EQ(mid.alpha, 1.0f);
    EXPECT_NEAR(a.mix(Oklcha::lch(0.5f, 0.1f, 30.0f), 0.5f).hue, 10.0f, 1e-5f);
    EXPECT_TRUE(a.mix(b, 0.0f) == a);
    EXPECT_TRUE(a.mix(b, 1.0f) == b);

    Oklcha assigned = a;
    assigned.mix_assign(b, 0.5f);
    EXPECT_TRUE(assigned == mid);

    // `Oklcha::sequential_dispersed` (oklcha.rs:94-104): the golden-ratio stride over the u32
    // domain, mapped to degrees.
    const Oklcha first = Oklcha::sequential_dispersed(0u);
    EXPECT_FLOAT_EQ(first.lightness, 0.75f);
    EXPECT_FLOAT_EQ(first.chroma, 0.1f);
    EXPECT_FLOAT_EQ(first.hue, 0.0f);
    EXPECT_FLOAT_EQ(first.alpha, 1.0f);
    EXPECT_NEAR(Oklcha::sequential_dispersed(1u).hue, 222.4922f, 1e-2f);
    for (std::uint32_t index = 0; index < 8; ++index) {
        const Oklcha color = Oklcha::sequential_dispersed(index);
        EXPECT_GE(color.hue, 0.0f);
        EXPECT_LT(color.hue, 360.0f);
    }
}

#include <gtest/gtest.h>
#include <epix/color.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

using namespace epix::color;

/** @brief `HWB <-> sRGB` round trip and the hue branches of `Hwba(const Srgba&)` — Bevy's
 *  `test_to_from_srgba` (`bevy_color/src/hwba.rs:307-343`) plus the primary and negative-hue
 *  cases of the conversion at `hwba.rs:194-230`. */
TEST(Hwba, ToFromSrgbaRoundTrip) {
    struct HwbCase {
        Srgba srgb;
        float hue;
        float whiteness;
        float blackness;
    };
    // The primaries take the red/green/blue `x_max` branches; the mixed colors exercise the
    // `chroma`-divided arms and, for rgb(1, 0.25, 0.5), the `hue < 0.0 -> 360.0 + hue` wrap
    // (`hwba.rs:210-228`); the gray has zero chroma, so hue takes the explicit zero branch.
    const std::array<HwbCase, 6> cases = {{
        {Srgba::rgb(1.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f},
        {Srgba::rgb(0.0f, 1.0f, 0.0f), 120.0f, 0.0f, 0.0f},
        {Srgba::rgb(0.0f, 0.0f, 1.0f), 240.0f, 0.0f, 0.0f},
        {Srgba::rgb(1.0f, 0.5f, 0.25f), 20.0f, 0.25f, 0.0f},
        {Srgba::rgb(1.0f, 0.25f, 0.5f), 340.0f, 0.25f, 0.0f},
        {Srgba::rgb(0.5f, 0.5f, 0.5f), 0.0f, 0.5f, 0.5f},
    }};

    for (const HwbCase& test_case : cases) {
        const Hwba hwb = Hwba(test_case.srgb);
        EXPECT_NEAR(hwb.hue, test_case.hue, 1e-5f);
        EXPECT_NEAR(hwb.whiteness, test_case.whiteness, 1e-5f);
        EXPECT_NEAR(hwb.blackness, test_case.blackness, 1e-5f);
        EXPECT_FLOAT_EQ(hwb.alpha, test_case.srgb.alpha);

        // Back through `Srgba(const Hwba&)` (`hwba.rs:232-267`), Bevy's round trip assertion.
        const Srgba back = Srgba(hwb);
        EXPECT_NEAR(back.red, test_case.srgb.red, 1e-5f);
        EXPECT_NEAR(back.green, test_case.srgb.green, 1e-5f);
        EXPECT_NEAR(back.blue, test_case.srgb.blue, 1e-5f);
        EXPECT_FLOAT_EQ(back.alpha, test_case.srgb.alpha);
    }
}

/** @brief `Default`, the `Gray` constants, and the `Hue`/`Alpha` accessors of `Hwba`
 *  (`bevy_color/src/hwba.rs:82-86`, `:101-104`, `:106-137`). */
TEST(Hwba, DefaultsConstantsAndAccessors) {
    // Unlike `Hsva`/`Hsla`, HWB's `Default` is black: `Self::new(0., 0., 1., 1.)`.
    EXPECT_EQ(Hwba(), Hwba::BLACK);
    EXPECT_FLOAT_EQ(Hwba().whiteness, 0.0f);
    EXPECT_FLOAT_EQ(Hwba().blackness, 1.0f);
    EXPECT_EQ(Hwba::BLACK, Hwba(0.0f, 0.0f, 1.0f, 1.0f));
    EXPECT_EQ(Hwba::WHITE, Hwba(0.0f, 1.0f, 0.0f, 1.0f));

    // `Gray::gray` is `BLACK.mix(&WHITE, lightness)` (`color_ops.rs:46-56`).
    EXPECT_EQ(Hwba::gray(0.0f), Hwba::BLACK);
    EXPECT_EQ(Hwba::gray(1.0f), Hwba::WHITE);

    const Hwba color = Hwba::hwb(30.0f, 0.25f, 0.5f);
    EXPECT_FLOAT_EQ(color.alpha, 1.0f);
    EXPECT_FLOAT_EQ(color.get_hue(), 30.0f);
    EXPECT_EQ(color.with_hue(90.0f), Hwba::hwb(90.0f, 0.25f, 0.5f));
    EXPECT_FLOAT_EQ(color.with_whiteness(0.75f).whiteness, 0.75f);
    EXPECT_FLOAT_EQ(color.with_blackness(0.75f).blackness, 0.75f);

    Hwba mutated = color;
    mutated.set_hue(120.0f);
    EXPECT_FLOAT_EQ(mutated.get_hue(), 120.0f);
    mutated.set_alpha(0.5f);
    EXPECT_FLOAT_EQ(mutated.alpha, 0.5f);

    // `Hue::rotate_hue` is `with_hue(rem_euclid(hue + degrees, 360.))` (`color_ops.rs:106-109`).
    EXPECT_FLOAT_EQ(color.rotate_hue(90.0f).get_hue(), 120.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(-90.0f).get_hue(), 300.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(180.0f).get_hue(), 210.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(0.0f).get_hue(), 30.0f);

    EXPECT_FLOAT_EQ(color.get_alpha(), 1.0f);
    EXPECT_TRUE(color.is_fully_opaque());
    EXPECT_FALSE(color.is_fully_transparent());
    EXPECT_TRUE(color.with_alpha(0.0f).is_fully_transparent());
    EXPECT_FALSE(color.with_alpha(0.0f).is_fully_opaque());

    EXPECT_EQ(Hwba(color.f32_array()), color);
    EXPECT_EQ(Hwba(color.f32_array_no_alpha()), color);
    EXPECT_EQ(Hwba(color.vec4()), color);
    EXPECT_EQ(Hwba(color.vec3()), color);
}

/** @brief `Default`, the `Gray` constants and the `Saturation` accessors of `Hsva`
 *  (`bevy_color/src/hsva.rs:79-83`, `:98-101`, `:137-155`). */
TEST(Hsva, DefaultsConstantsAndAccessors) {
    // HSV's `Default` is white: `Self::new(0., 0., 1., 1.)` (`hsva.rs:79-83`).
    EXPECT_EQ(Hsva(), Hsva::WHITE);
    EXPECT_FLOAT_EQ(Hsva().saturation, 0.0f);
    EXPECT_FLOAT_EQ(Hsva().value, 1.0f);
    EXPECT_EQ(Hsva::BLACK, Hsva(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(Hsva::WHITE, Hsva(0.0f, 0.0f, 1.0f, 1.0f));

    EXPECT_EQ(Hsva::gray(0.0f), Hsva::BLACK);
    EXPECT_EQ(Hsva::gray(1.0f), Hsva::WHITE);

    const Hsva color = Hsva::hsv(180.0f, 0.25f, 0.5f);
    EXPECT_FLOAT_EQ(color.alpha, 1.0f);
    EXPECT_FLOAT_EQ(color.get_saturation(), 0.25f);
    EXPECT_EQ(color.with_saturation(0.75f), Hsva::hsv(180.0f, 0.75f, 0.5f));
    EXPECT_FLOAT_EQ(color.with_value(1.0f).value, 1.0f);

    Hsva mutated = color;
    mutated.set_saturation(0.5f);
    EXPECT_FLOAT_EQ(mutated.get_saturation(), 0.5f);
    mutated.set_hue(90.0f);
    EXPECT_FLOAT_EQ(mutated.get_hue(), 90.0f);

    EXPECT_FLOAT_EQ(color.rotate_hue(-90.0f).get_hue(), 90.0f);
    EXPECT_FALSE(color.is_fully_transparent());
    EXPECT_TRUE(color.is_fully_opaque());

    EXPECT_EQ(Hsva(color.f32_array()), color);
    EXPECT_EQ(Hsva(color.f32_array_no_alpha()), color);
    EXPECT_EQ(Hsva(color.vec4()), color);
    EXPECT_EQ(Hsva(color.vec3()), color);
}

/** @brief `HSV <-> HWB` (`bevy_color/src/hsva.rs:157-172` and `:174-193`), including the
 *  `value == 0` saturation guard. */
TEST(Hsva, HwbaRoundTrip) {
    const Hsva hsv = Hsva(180.0f, 0.5f, 0.5f, 1.0f);

    const Hwba hwb = Hwba(hsv);
    EXPECT_NEAR(hwb.hue, 180.0f, 1e-5f);
    EXPECT_NEAR(hwb.whiteness, 0.25f, 1e-5f);  // (1 - saturation) * value
    EXPECT_NEAR(hwb.blackness, 0.5f, 1e-5f);   // 1 - value
    EXPECT_FLOAT_EQ(hwb.alpha, 1.0f);

    const Hsva back = Hsva(hwb);
    EXPECT_NEAR(back.hue, 180.0f, 1e-5f);
    EXPECT_NEAR(back.saturation, 0.5f, 1e-5f);
    EXPECT_NEAR(back.value, 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(back.alpha, 1.0f);

    // Black in HSV is black in HWB, and the round trip exercises the `value == 0` guard.
    EXPECT_EQ(Hwba(Hsva::BLACK), Hwba::BLACK);
    EXPECT_EQ(Hsva(Hwba(0.0f, 1.0f, 1.0f, 1.0f)), Hsva(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(Hsva(Hwba::WHITE), Hsva::WHITE);

    // The `Luminance`-free spaces still mix along the shortest hue arc (`hsva.rs:85-96`).
    EXPECT_FLOAT_EQ(Hsva(10.0f, 0.5f, 0.5f, 1.0f).mix(Hsva(20.0f, 0.5f, 0.5f, 1.0f), 0.5f).hue, 15.0f);
    EXPECT_FLOAT_EQ(Hsva(10.0f, 0.5f, 0.5f, 1.0f).mix(Hsva(350.0f, 0.5f, 0.5f, 1.0f), 0.5f).hue, 0.0f);
}

/** @brief `Default`, the `Gray` constants, `Luminance` and `Saturation` of `Hsla`
 *  (`bevy_color/src/hsla.rs:108-112`, `:127-130`, `:166-209`). */
TEST(Hsla, DefaultsConstantsAndLuminance) {
    // HSL's `Default` is white: `Self::new(0., 0., 1., 1.)` (`hsla.rs:108-112`).
    EXPECT_EQ(Hsla(), Hsla::WHITE);
    EXPECT_FLOAT_EQ(Hsla().lightness, 1.0f);
    EXPECT_EQ(Hsla::BLACK, Hsla(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(Hsla::WHITE, Hsla(0.0f, 0.0f, 1.0f, 1.0f));

    EXPECT_EQ(Hsla::gray(0.0f), Hsla::BLACK);
    EXPECT_EQ(Hsla::gray(1.0f), Hsla::WHITE);

    const Hsla color = Hsla::hsl(120.0f, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(color.get_saturation(), 0.5f);
    EXPECT_EQ(color.with_saturation(0.25f), Hsla::hsl(120.0f, 0.25f, 0.5f));
    EXPECT_EQ(color.with_lightness(0.75f), Hsla::hsl(120.0f, 0.5f, 0.75f));

    // For HSL the luminance is the lightness itself (`hsla.rs:186-194`).
    EXPECT_FLOAT_EQ(color.luminance(), 0.5f);
    EXPECT_EQ(color.with_luminance(0.25f), Hsla::hsl(120.0f, 0.5f, 0.25f));

    // `darker` clamps to `[0.0, 1.0]`; `lighter` only caps at 1.0 (`hsla.rs:196-208`).
    EXPECT_NEAR(color.darker(0.2f).lightness, 0.3f, 1e-5f);
    EXPECT_FLOAT_EQ(color.darker(1.0f).lightness, 0.0f);
    EXPECT_NEAR(color.lighter(0.2f).lightness, 0.7f, 1e-5f);
    EXPECT_FLOAT_EQ(color.lighter(1.0f).lightness, 1.0f);

    Hsla mutated = color;
    mutated.set_saturation(0.75f);
    EXPECT_FLOAT_EQ(mutated.get_saturation(), 0.75f);

    EXPECT_EQ(Hsla(color.f32_array()), color);
    EXPECT_EQ(Hsla(color.f32_array_no_alpha()), color);
    EXPECT_EQ(Hsla(color.vec4()), color);
    EXPECT_EQ(Hsla(color.vec3()), color);
}

/** @brief `HSL <-> HSV` (`bevy_color/src/hsla.rs:265-284` and `:286-305`), including the
 *  `lightness == 0.0 || lightness == 1.0` saturation guard. */
TEST(Hsla, HsvaRoundTrip) {
    const Hsla hsl = Hsla(0.5f, 0.5f, 0.5f, 1.0f);

    const Hsva hsv = Hsva(hsl);
    EXPECT_NEAR(hsv.hue, 0.5f, 1e-5f);
    EXPECT_NEAR(hsv.saturation, 2.0f / 3.0f, 1e-5f);  // 2 * (1 - lightness / value)
    EXPECT_NEAR(hsv.value, 0.75f, 1e-5f);             // lightness + saturation * min(l, 1 - l)
    EXPECT_FLOAT_EQ(hsv.alpha, 1.0f);

    const Hsla back = Hsla(hsv);
    EXPECT_NEAR(back.hue, 0.5f, 1e-5f);
    EXPECT_NEAR(back.saturation, 0.5f, 1e-5f);
    EXPECT_NEAR(back.lightness, 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(back.alpha, 1.0f);

    // The guards: a black or white HSV has zero saturation in HSL whatever its saturation channel.
    EXPECT_EQ(Hsla(Hsva(0.0f, 0.0f, 0.0f, 1.0f)), Hsla::BLACK);
    EXPECT_EQ(Hsla(Hsva(0.0f, 1.0f, 0.0f, 1.0f)), Hsla::BLACK);
    EXPECT_EQ(Hsla(Hsva::WHITE), Hsla::WHITE);
    EXPECT_EQ(Hsla(Hwba::BLACK), Hsla::BLACK);
    EXPECT_EQ(Hsla(Hwba::WHITE), Hsla::WHITE);
}

/** @brief `Hsla::sequential_dispersed` and its two constants (`bevy_color/src/hsla.rs:78-105`);
 *  the hues are Bevy's own `test_from_index` expectations (`hsla.rs:439-454`). */
TEST(Hsla, SequentialDispersed) {
    const Hsla first = Hsla::sequential_dispersed(0);
    EXPECT_FLOAT_EQ(first.hue, 0.0f);
    EXPECT_FLOAT_EQ(first.saturation, 1.0f);
    EXPECT_FLOAT_EQ(first.lightness, 0.5f);
    EXPECT_FLOAT_EQ(first.alpha, 1.0f);

    EXPECT_NEAR(Hsla::sequential_dispersed(1).hue, 222.49225f, 1e-3f);
    EXPECT_NEAR(Hsla::sequential_dispersed(2).hue, 84.984474f, 1e-3f);
    EXPECT_NEAR(Hsla::sequential_dispersed(3).hue, 307.4767f, 1e-3f);
    EXPECT_NEAR(Hsla::sequential_dispersed(4).hue, 169.96895f, 1e-3f);

    // Every index yields a fully saturated, mid-lightness color in range.
    for (std::uint32_t index = 0; index < 8; ++index) {
        const Hsla color = Hsla::sequential_dispersed(index);
        EXPECT_GE(color.hue, 0.0f);
        EXPECT_LT(color.hue, 360.0f);
        EXPECT_FLOAT_EQ(color.saturation, 1.0f);
        EXPECT_FLOAT_EQ(color.lightness, 0.5f);
    }
}

/** @brief `Mix` interpolation along the shortest hue arc and `Hue::rotate_hue` — Bevy's
 *  `test_mix_wrap` (`bevy_color/src/hsla.rs:418-437`) and the `rotate_hue` assertions of
 *  `color_ops.rs:174-184`. */
TEST(Hsla, MixWrapAndRotateHue) {
    const Hsla hsla1 = Hsla(10.0f, 0.5f, 0.5f, 1.0f);
    const Hsla hsla2 = Hsla(20.0f, 0.5f, 0.5f, 1.0f);
    const Hsla hsla3 = Hsla(350.0f, 0.5f, 0.5f, 1.0f);

    EXPECT_FLOAT_EQ(hsla1.mix(hsla2, 0.25f).hue, 12.5f);
    EXPECT_FLOAT_EQ(hsla1.mix(hsla2, 0.5f).hue, 15.0f);
    EXPECT_FLOAT_EQ(hsla1.mix(hsla2, 0.75f).hue, 17.5f);
    EXPECT_FLOAT_EQ(hsla2.mix(hsla1, 0.25f).hue, 17.5f);
    EXPECT_FLOAT_EQ(hsla2.mix(hsla1, 0.5f).hue, 15.0f);
    EXPECT_FLOAT_EQ(hsla2.mix(hsla1, 0.75f).hue, 12.5f);

    // 10 and 350 are 20 degrees apart the short way, across the seam.
    EXPECT_FLOAT_EQ(hsla1.mix(hsla3, 0.25f).hue, 5.0f);
    EXPECT_FLOAT_EQ(hsla1.mix(hsla3, 0.5f).hue, 0.0f);
    EXPECT_FLOAT_EQ(hsla1.mix(hsla3, 0.75f).hue, 355.0f);
    EXPECT_FLOAT_EQ(hsla3.mix(hsla1, 0.25f).hue, 355.0f);
    EXPECT_FLOAT_EQ(hsla3.mix(hsla1, 0.5f).hue, 0.0f);
    EXPECT_FLOAT_EQ(hsla3.mix(hsla1, 0.75f).hue, 5.0f);

    // Every other channel mixes linearly, alpha included (`hsla.rs:114-125`).
    const Hsla mixed = Hsla(10.0f, 0.0f, 0.25f, 0.5f).mix(Hsla(20.0f, 1.0f, 0.75f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(mixed.hue, 15.0f);
    EXPECT_NEAR(mixed.saturation, 0.5f, 1e-5f);
    EXPECT_NEAR(mixed.lightness, 0.5f, 1e-5f);
    EXPECT_NEAR(mixed.alpha, 0.75f, 1e-5f);

    Hsla assigned = hsla1;
    assigned.mix_assign(hsla3, 0.25f);
    EXPECT_FLOAT_EQ(assigned.hue, 5.0f);

    const Hsla rotated = Hsla::hsl(180.0f, 1.0f, 0.5f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(90.0f).hue, 270.0f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(-90.0f).hue, 90.0f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(180.0f).hue, 0.0f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(-180.0f).hue, 0.0f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(0.0f).hue, 180.0f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(360.0f).hue, 180.0f);
    EXPECT_FLOAT_EQ(rotated.rotate_hue(-360.0f).hue, 180.0f);
}

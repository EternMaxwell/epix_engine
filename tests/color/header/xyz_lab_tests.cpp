#include <gtest/gtest.h>

#include <array>

#include <epix/color.hpp>

using namespace epix::color;

TEST(Xyza, LinearRgbaMatrixRows) {
    const Xyza red = Xyza(LinearRgba::RED);
    EXPECT_FLOAT_EQ(red.x, 0.4124564f);
    EXPECT_FLOAT_EQ(red.y, 0.2126729f);
    EXPECT_FLOAT_EQ(red.z, 0.0193339f);
    EXPECT_FLOAT_EQ(red.alpha, 1.0f);

    const Xyza green = Xyza(LinearRgba::GREEN);
    EXPECT_FLOAT_EQ(green.x, 0.3575761f);
    EXPECT_FLOAT_EQ(green.y, 0.7151522f);
    EXPECT_FLOAT_EQ(green.z, 0.119192f);

    const Xyza blue = Xyza(LinearRgba::BLUE);
    EXPECT_FLOAT_EQ(blue.x, 0.1804375f);
    EXPECT_FLOAT_EQ(blue.y, 0.072175f);
    EXPECT_FLOAT_EQ(blue.z, 0.9503041f);

    // The y row is the CIE luminance, so it tracks LinearRgba::luminance's rounding of the same
    // weighted sum.
    const LinearRgba mixed(0.3f, 0.6f, 0.2f, 1.0f);
    EXPECT_NEAR(Xyza(mixed).y, mixed.luminance(), 1e-3f);
}

TEST(Xyza, LinearRgbaRoundTrip) {
    const Xyza original(0.5f, 0.5f, 0.5f, 1.0f);

    const LinearRgba linear = LinearRgba(original);
    EXPECT_NEAR(linear.red, 0.6023922f, 1e-5f);
    EXPECT_NEAR(linear.green, 0.4741504f, 1e-5f);
    EXPECT_NEAR(linear.blue, 0.4544214f, 1e-5f);

    const Xyza round_trip = Xyza(linear);
    EXPECT_NEAR(round_trip.x, original.x, 1e-5f);
    EXPECT_NEAR(round_trip.y, original.y, 1e-5f);
    EXPECT_NEAR(round_trip.z, original.z, 1e-5f);
    EXPECT_FLOAT_EQ(round_trip.alpha, original.alpha);
}

TEST(Xyza, DefaultsAndConstants) {
    const Xyza default_color;
    EXPECT_FLOAT_EQ(default_color.x, 0.0f);
    EXPECT_FLOAT_EQ(default_color.y, 0.0f);
    EXPECT_FLOAT_EQ(default_color.z, 0.0f);
    EXPECT_FLOAT_EQ(default_color.alpha, 1.0f);

    EXPECT_TRUE(default_color == Xyza::BLACK);
    EXPECT_TRUE(Xyza::WHITE == Xyza::D65_WHITE);
    EXPECT_FLOAT_EQ(Xyza::D65_WHITE.x, 0.95047f);
    EXPECT_FLOAT_EQ(Xyza::D65_WHITE.y, 1.0f);
    EXPECT_FLOAT_EQ(Xyza::D65_WHITE.z, 1.08883f);

    const Xyza shorthand = Xyza::xyz(0.1f, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(shorthand.alpha, 1.0f);
    EXPECT_TRUE(shorthand == Xyza(0.1f, 0.2f, 0.3f, 1.0f));

    const Xyza gray = Xyza::gray(0.5f);
    EXPECT_FLOAT_EQ(gray.x, 0.475235f);
    EXPECT_FLOAT_EQ(gray.y, 0.5f);
    EXPECT_FLOAT_EQ(gray.z, 0.544415f);
    EXPECT_FLOAT_EQ(gray.alpha, 1.0f);

    const Xyza components = Xyza(std::array<float, 3>{0.1f, 0.2f, 0.3f});
    EXPECT_FLOAT_EQ(components.alpha, 1.0f);
    EXPECT_TRUE(components == shorthand);

    const std::array<float, 4> components_array = components.f32_array();
    EXPECT_FLOAT_EQ(components_array[0], 0.1f);
    EXPECT_FLOAT_EQ(components_array[1], 0.2f);
    EXPECT_FLOAT_EQ(components_array[2], 0.3f);
    EXPECT_FLOAT_EQ(components_array[3], 1.0f);
}

TEST(Xyza, LuminanceAndAlpha) {
    const Xyza color = Xyza::xyz(0.1f, 0.4f, 0.2f);
    EXPECT_FLOAT_EQ(color.luminance(), color.y);

    const Xyza brightened = color.with_luminance(0.8f);
    EXPECT_FLOAT_EQ(brightened.y, 0.8f);
    EXPECT_FLOAT_EQ(brightened.x, color.x);
    EXPECT_FLOAT_EQ(brightened.luminance(), 0.8f);

    EXPECT_NEAR(color.darker(0.1f).y, 0.3f, 1e-6f);
    EXPECT_FLOAT_EQ(color.darker(0.9f).y, 0.0f);
    EXPECT_FLOAT_EQ(color.lighter(0.9f).y, 1.0f);
    EXPECT_FLOAT_EQ(color.lighter(0.1f).y, 0.5f);

    EXPECT_TRUE(color.is_fully_opaque());
    EXPECT_FALSE(color.is_fully_transparent());
    EXPECT_TRUE(color.with_alpha(0.0f).is_fully_transparent());
    EXPECT_FLOAT_EQ(color.with_alpha(0.25f).get_alpha(), 0.25f);

    Xyza mutated = color;
    mutated.set_alpha(0.75f);
    EXPECT_FLOAT_EQ(mutated.get_alpha(), 0.75f);
}

TEST(Laba, FromXyza) {
    // Test red, whose XYZ happens to be the exact first column of Bevy's LinearRgba -> XYZ matrix.
    const Xyza red_xyz = Xyza(LinearRgba::RED);

    const Laba red = Laba(red_xyz);
    EXPECT_NEAR(red.lightness, 0.532408f, 1e-5f);
    EXPECT_NEAR(red.a, 0.8009243f, 1e-5f);
    EXPECT_NEAR(red.b, 0.6720321f, 1e-5f);
    EXPECT_FLOAT_EQ(red.alpha, 1.0f);

    // The D65 white point is Lab (1, 0, 0) by construction.
    const Laba white = Laba(Xyza::D65_WHITE);
    EXPECT_NEAR(white.lightness, 1.0f, 1e-6f);
    EXPECT_NEAR(white.a, 0.0f, 1e-6f);
    EXPECT_NEAR(white.b, 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(white.alpha, 1.0f);
}

TEST(Laba, XyzaRoundTrip) {
    const Xyza original = Xyza::xyz(0.4124564f, 0.2126729f, 0.0193339f);
    const Xyza round_trip = Xyza(Laba(original));
    EXPECT_NEAR(round_trip.x, original.x, 1e-5f);
    EXPECT_NEAR(round_trip.y, original.y, 1e-5f);
    EXPECT_NEAR(round_trip.z, original.z, 1e-5f);
    EXPECT_FLOAT_EQ(round_trip.alpha, 1.0f);

    // Below CIE_EPSILON both directions take the linear branch; it must still round trip.
    const Xyza dark = Xyza::xyz(0.0001f, 0.0001f, 0.0001f);
    const Laba dark_lab = Laba(dark);
    EXPECT_NEAR(dark_lab.lightness, 0.0009033f, 1e-5f);
    EXPECT_NEAR(dark_lab.a, 0.0002030f, 1e-5f);
    EXPECT_NEAR(dark_lab.b, 0.0001270f, 1e-5f);

    const Xyza dark_round_trip = Xyza(dark_lab);
    EXPECT_NEAR(dark_round_trip.x, dark.x, 1e-5f);
    EXPECT_NEAR(dark_round_trip.y, dark.y, 1e-5f);
    EXPECT_NEAR(dark_round_trip.z, dark.z, 1e-5f);
}

TEST(Laba, DefaultsAndConstants) {
    const Laba default_color;
    EXPECT_FLOAT_EQ(default_color.lightness, 1.0f);
    EXPECT_FLOAT_EQ(default_color.a, 0.0f);
    EXPECT_FLOAT_EQ(default_color.b, 0.0f);
    EXPECT_FLOAT_EQ(default_color.alpha, 1.0f);
    EXPECT_TRUE(default_color == Laba::WHITE);

    EXPECT_TRUE(Laba::BLACK == Laba::lab(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(Laba::BLACK.alpha, 1.0f);
    EXPECT_TRUE(Laba::WHITE == Laba(1.0f, 0.0f, 0.0f, 1.0f));

    EXPECT_NEAR(Laba::CIE_EPSILON, 216.0f / 24389.0f, 1e-9f);
    EXPECT_NEAR(Laba::CIE_KAPPA, 24389.0f / 27.0f, 1e-3f);

    const Laba chromatic = Laba::lab(0.5f, 0.1f, -0.1f);
    EXPECT_FLOAT_EQ(chromatic.alpha, 1.0f);
    EXPECT_FLOAT_EQ(chromatic.with_lightness(0.75f).lightness, 0.75f);
    EXPECT_FLOAT_EQ(chromatic.with_lightness(0.75f).a, 0.1f);
}

TEST(Laba, LuminanceMixAndGray) {
    const Laba color = Laba::lab(0.5f, 0.1f, -0.1f);
    EXPECT_FLOAT_EQ(color.luminance(), color.lightness);
    EXPECT_FLOAT_EQ(color.with_luminance(0.8f).luminance(), 0.8f);
    EXPECT_FLOAT_EQ(color.with_luminance(0.8f).b, color.b);

    EXPECT_FLOAT_EQ(color.darker(0.25f).lightness, 0.25f);
    EXPECT_FLOAT_EQ(color.darker(2.0f).lightness, 0.0f);
    EXPECT_FLOAT_EQ(color.lighter(0.25f).lightness, 0.75f);
    EXPECT_FLOAT_EQ(color.lighter(2.0f).lightness, 1.0f);

    const Laba mixed = Laba::BLACK.mix(Laba::WHITE, 0.25f);
    EXPECT_FLOAT_EQ(mixed.lightness, 0.25f);
    EXPECT_FLOAT_EQ(mixed.a, 0.0f);
    EXPECT_FLOAT_EQ(mixed.b, 0.0f);
    EXPECT_FLOAT_EQ(mixed.alpha, 1.0f);

    EXPECT_TRUE(Laba::gray(0.5f) == Laba::lab(0.5f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(Laba::gray(0.25f).alpha, 1.0f);

    Laba assigned = Laba::BLACK;
    assigned.mix_assign(Laba::WHITE, 0.5f);
    EXPECT_TRUE(assigned == Laba::lab(0.5f, 0.0f, 0.0f));

    // Laba owns the conversion that produces it from the polar space.
    const Laba from_lch = Laba(Lcha(0.5f, 0.4f, 210.0f, 0.5f));
    EXPECT_FLOAT_EQ(from_lch.lightness, 0.5f);
    EXPECT_NEAR(from_lch.a, -0.3464102f, 1e-5f);
    EXPECT_NEAR(from_lch.b, -0.2f, 1e-5f);
    EXPECT_FLOAT_EQ(from_lch.alpha, 0.5f);
}

TEST(Lcha, FromLaba) {
    const Lcha polar = Lcha(Laba::lab(0.5f, 0.4f, 0.0f));
    EXPECT_FLOAT_EQ(polar.lightness, 0.5f);
    EXPECT_NEAR(polar.chroma, 0.4f, 1e-6f);
    EXPECT_FLOAT_EQ(polar.hue, 0.0f);
    EXPECT_FLOAT_EQ(polar.alpha, 1.0f);

    // atan2 returns negative angles for the lower half plane; Bevy shifts them by +360.
    EXPECT_NEAR(Lcha(Laba::lab(0.5f, -0.4f, 0.0f)).hue, 180.0f, 1e-4f);
    EXPECT_NEAR(Lcha(Laba::lab(0.5f, -0.4f, 0.4f)).hue, 135.0f, 1e-4f);
    EXPECT_NEAR(Lcha(Laba::lab(0.5f, 0.4f, -0.4f)).hue, 315.0f, 1e-4f);

    // The radial chroma is clamped into [0.0, 1.5].
    const Lcha clamped = Lcha(Laba::lab(0.5f, 3.0f, 4.0f));
    EXPECT_FLOAT_EQ(clamped.chroma, 1.5f);
    EXPECT_NEAR(clamped.hue, 53.130104f, 1e-5f);
}

TEST(Lcha, LabaRoundTrip) {
    const Lcha original(0.5f, 0.4f, 210.0f, 0.5f);
    const Lcha round_trip = Lcha(Laba(original));
    EXPECT_FLOAT_EQ(round_trip.lightness, original.lightness);
    EXPECT_NEAR(round_trip.chroma, original.chroma, 1e-5f);
    EXPECT_NEAR(round_trip.hue, original.hue, 1e-4f);
    EXPECT_FLOAT_EQ(round_trip.alpha, original.alpha);

    // Red through the whole chain XYZ -> Lab -> LCH.
    const Lcha red = Lcha(Laba(Xyza(LinearRgba::RED)));
    EXPECT_NEAR(red.lightness, 0.532408f, 1e-5f);
    EXPECT_NEAR(red.chroma, 1.0455174f, 1e-5f);
    EXPECT_NEAR(red.hue, 39.999f, 1e-3f);
}

TEST(Lcha, DefaultsAndConstants) {
    const Lcha default_color;
    EXPECT_FLOAT_EQ(default_color.lightness, 1.0f);
    EXPECT_FLOAT_EQ(default_color.chroma, 0.0f);
    EXPECT_FLOAT_EQ(default_color.hue, 0.0f);
    EXPECT_FLOAT_EQ(default_color.alpha, 1.0f);

    EXPECT_FLOAT_EQ(Lcha::BLACK.lightness, 0.0f);
    EXPECT_FLOAT_EQ(Lcha::WHITE.lightness, 1.0f);
    EXPECT_FLOAT_EQ(Lcha::BLACK.chroma, 0.0f);
    EXPECT_FLOAT_EQ(Lcha::WHITE.chroma, 0.0f);
    EXPECT_FLOAT_EQ(Lcha::BLACK.hue, 0.0000136603785f);
    EXPECT_FLOAT_EQ(Lcha::WHITE.hue, 0.0000136603785f);

    const Lcha shorthand = Lcha::lch(0.75f, 0.35f, 120.0f);
    EXPECT_FLOAT_EQ(shorthand.alpha, 1.0f);
    EXPECT_TRUE(shorthand == Lcha(0.75f, 0.35f, 120.0f, 1.0f));
    EXPECT_FLOAT_EQ(shorthand.with_chroma(0.5f).chroma, 0.5f);
    EXPECT_FLOAT_EQ(shorthand.with_chroma(0.5f).hue, 120.0f);

    const Lcha dispersed = Lcha::sequential_dispersed(0);
    EXPECT_TRUE(dispersed == Lcha::lch(0.75f, 0.35f, 0.0f));
    EXPECT_NEAR(Lcha::sequential_dispersed(1).hue, 222.49225f, 1e-3f);
    EXPECT_NEAR(Lcha::sequential_dispersed(2).hue, 84.984474f, 1e-3f);
}

TEST(Lcha, HueLuminanceAndMix) {
    const Lcha color = Lcha::lch(0.5f, 0.4f, 20.0f);
    EXPECT_FLOAT_EQ(color.get_hue(), color.hue);
    EXPECT_FLOAT_EQ(color.with_hue(200.0f).hue, 200.0f);
    EXPECT_FLOAT_EQ(color.with_hue(200.0f).chroma, color.chroma);

    // rotate_hue is the trait's provided method: rem_euclid(hue + degrees, 360).
    EXPECT_FLOAT_EQ(color.rotate_hue(350.0f).hue, 10.0f);
    EXPECT_FLOAT_EQ(color.rotate_hue(-40.0f).hue, 340.0f);

    EXPECT_FLOAT_EQ(color.luminance(), color.lightness);
    EXPECT_FLOAT_EQ(color.with_luminance(0.9f).luminance(), 0.9f);
    EXPECT_FLOAT_EQ(color.darker(2.0f).lightness, 0.0f);
    EXPECT_FLOAT_EQ(color.lighter(2.0f).lightness, 1.0f);

    // Mix interpolates the hue along the shortest arc, and the rest componentwise.
    const Lcha mixed = Lcha::lch(0.5f, 0.4f, 350.0f).mix(Lcha::lch(0.5f, 0.4f, 10.0f), 0.5f);
    EXPECT_FLOAT_EQ(mixed.hue, 0.0f);
    EXPECT_FLOAT_EQ(mixed.lightness, 0.5f);
    EXPECT_FLOAT_EQ(mixed.chroma, 0.4f);

    const Lcha gray = Lcha::gray(0.5f);
    EXPECT_FLOAT_EQ(gray.lightness, 0.5f);
    EXPECT_FLOAT_EQ(gray.chroma, 0.0f);
    EXPECT_FLOAT_EQ(gray.hue, 0.0000136603785f);

    Lcha mutated = color;
    mutated.set_hue(15.0f);
    EXPECT_FLOAT_EQ(mutated.get_hue(), 15.0f);
    EXPECT_TRUE(mutated.with_alpha(0.0f).is_fully_transparent());
    EXPECT_TRUE(color.is_fully_opaque());
}

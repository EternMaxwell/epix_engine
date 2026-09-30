#include <gtest/gtest.h>

#include <epix/color.hpp>
#include <cmath>
#include <type_traits>
#include <variant>

using namespace epix::color;

namespace basic = epix::color::palettes::basic;

// ===========================================================================
// Color - the const factories and the variant they select
// ===========================================================================

TEST(Color, FactoriesStoreTheExpectedSpace) {
    EXPECT_TRUE(std::holds_alternative<Srgba>(Color::srgba(0.1f, 0.2f, 0.3f, 0.4f).color));
    EXPECT_TRUE(std::holds_alternative<Srgba>(Color::srgb(0.1f, 0.2f, 0.3f).color));
    EXPECT_TRUE(std::holds_alternative<Srgba>(Color::srgb_from_array({0.1f, 0.2f, 0.3f}).color));
    EXPECT_TRUE(std::holds_alternative<Srgba>(Color::srgb_u8(255, 128, 0).color));
    EXPECT_TRUE(std::holds_alternative<LinearRgba>(Color::linear_rgba(0.1f, 0.2f, 0.3f, 0.4f).color));
    EXPECT_TRUE(std::holds_alternative<LinearRgba>(Color::linear_rgb(0.1f, 0.2f, 0.3f).color));
    EXPECT_TRUE(std::holds_alternative<Hsla>(Color::hsl(120.0f, 0.5f, 0.5f).color));
    EXPECT_TRUE(std::holds_alternative<Hsva>(Color::hsv(120.0f, 0.5f, 0.5f).color));
    EXPECT_TRUE(std::holds_alternative<Hwba>(Color::hwb(120.0f, 0.5f, 0.5f).color));
    EXPECT_TRUE(std::holds_alternative<Laba>(Color::lab(0.5f, 0.1f, -0.1f).color));
    EXPECT_TRUE(std::holds_alternative<Lcha>(Color::lch(0.5f, 0.1f, 120.0f).color));
    EXPECT_TRUE(std::holds_alternative<Oklaba>(Color::oklab(0.5f, 0.1f, -0.1f).color));
    EXPECT_TRUE(std::holds_alternative<Oklcha>(Color::oklch(0.5f, 0.1f, 120.0f).color));
    EXPECT_TRUE(std::holds_alternative<Xyza>(Color::xyz(0.1f, 0.2f, 0.3f).color));

    // The factories forward their arguments unchanged, with a defaulted alpha of 1.0.
    const Srgba srgb = std::get<Srgba>(Color::srgba(0.1f, 0.2f, 0.3f, 0.4f).color);
    EXPECT_FLOAT_EQ(srgb.red, 0.1f);
    EXPECT_FLOAT_EQ(srgb.green, 0.2f);
    EXPECT_FLOAT_EQ(srgb.blue, 0.3f);
    EXPECT_FLOAT_EQ(srgb.alpha, 0.4f);
    EXPECT_FLOAT_EQ(std::get<Srgba>(Color::srgb(0.1f, 0.2f, 0.3f).color).alpha, 1.0f);

    // Bevy's `u8 as f32 / 255.0`.
    const Srgba from_u8 = std::get<Srgba>(Color::srgba_u8(255, 0, 0, 51).color);
    EXPECT_FLOAT_EQ(from_u8.red, 1.0f);
    EXPECT_FLOAT_EQ(from_u8.alpha, 0.2f);

    const Xyza xyz = std::get<Xyza>(Color::xyza(0.1f, 0.2f, 0.3f, 0.4f).color);
    EXPECT_FLOAT_EQ(xyz.x, 0.1f);
    EXPECT_FLOAT_EQ(xyz.z, 0.3f);

    // An implicit converting constructor is all Bevy's derived `From<X> for Color` needs.
    EXPECT_TRUE(Color(Hsla::hsl(120.0f, 0.5f, 0.5f)) == Color::hsl(120.0f, 0.5f, 0.5f));
    EXPECT_TRUE(std::holds_alternative<Hsla>(Color(Hsla::hsl(120.0f, 0.5f, 0.5f)).color));
}

// ===========================================================================
// Color - default, the three constants and the Alpha contract
// ===========================================================================

TEST(Color, DefaultsConstantsAndAlpha) {
    const Color default_color;
    EXPECT_TRUE(std::holds_alternative<LinearRgba>(default_color.color));
    EXPECT_TRUE(default_color == Color::WHITE);
    EXPECT_TRUE(default_color == Color::linear_rgb(1.0f, 1.0f, 1.0f));

    // All three constants are LinearRgba-space, exactly as in Bevy.
    EXPECT_TRUE(std::holds_alternative<LinearRgba>(Color::WHITE.color));
    EXPECT_TRUE(std::holds_alternative<LinearRgba>(Color::BLACK.color));
    EXPECT_TRUE(std::holds_alternative<LinearRgba>(Color::NONE.color));
    EXPECT_TRUE(Color::WHITE == Color::linear_rgba(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(Color::BLACK == Color::linear_rgba(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(Color::NONE == Color::linear_rgba(0.0f, 0.0f, 0.0f, 0.0f));

    EXPECT_TRUE(Color::WHITE.is_fully_opaque());
    EXPECT_FALSE(Color::WHITE.is_fully_transparent());
    EXPECT_TRUE(Color::NONE.is_fully_transparent());
    EXPECT_FALSE(Color::NONE.is_fully_opaque());

    // with_alpha keeps the variant's own space; set_alpha is `*this = with_alpha(alpha)`.
    const Color semi = Color::srgb(1.0f, 0.0f, 0.0f).with_alpha(0.25f);
    EXPECT_TRUE(std::holds_alternative<Srgba>(semi.color));
    EXPECT_FLOAT_EQ(semi.alpha(), 0.25f);
    EXPECT_FLOAT_EQ(std::get<Srgba>(semi.color).red, 1.0f);

    Color mutated = Color::hsla(120.0f, 0.5f, 0.5f, 1.0f);
    mutated.set_alpha(0.5f);
    EXPECT_TRUE(std::holds_alternative<Hsla>(mutated.color));
    EXPECT_FLOAT_EQ(mutated.alpha(), 0.5f);
}

// ===========================================================================
// Color - linear() / srgba() are the spaces' own converting constructor from Color
// ===========================================================================

TEST(Color, LinearAndSrgbaAccessorsRoundTrip) {
    const Srgba srgb = Srgba::rgb(0.5f, 0.25f, 0.75f);
    const Color color(srgb);

    // The receiver's own space is returned unchanged.
    EXPECT_TRUE(color.srgba() == srgb);
    EXPECT_TRUE(color.linear() == LinearRgba(srgb));

    // sRGB -> linear -> sRGB round trips through the gamma functions.
    const Srgba back = Color(color.linear()).srgba();
    EXPECT_NEAR(back.red, srgb.red, 1e-5f);
    EXPECT_NEAR(back.green, srgb.green, 1e-5f);
    EXPECT_NEAR(back.blue, srgb.blue, 1e-5f);
    EXPECT_FLOAT_EQ(back.alpha, srgb.alpha);

    // A non-sRGB variant converts through that space's own converting constructor into
    // LinearRgba/Srgba.
    const Hsla hsl = Hsla::hsl(120.0f, 0.5f, 0.5f);
    EXPECT_TRUE(Color(hsl).srgba() == Srgba(hsl));
    EXPECT_TRUE(Color(hsl).linear() == LinearRgba(hsl));
}

// ===========================================================================
// Color - Luminance dispatch: native spaces vs the ChosenColorSpace fallback
// ===========================================================================

TEST(Color, LuminanceDispatch) {
    const Srgba srgb = Srgba::rgb(0.25f, 0.5f, 0.75f);
    // sRGB defines luminance natively.
    EXPECT_FLOAT_EQ(Color(srgb).luminance(), srgb.luminance());

    // HSV does not, so Bevy converts to the chosen space (Oklch) and back.
    const Hsva hsv = Hsva::hsv(120.0f, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(Color(hsv).luminance(), Oklcha(hsv).luminance());

    // with_luminance keeps the variant's own space in both cases.
    const Color srgb_result = Color(srgb).with_luminance(0.1f);
    EXPECT_TRUE(std::holds_alternative<Srgba>(srgb_result.color));
    EXPECT_FLOAT_EQ(std::get<Srgba>(srgb_result.color).red,
                    srgb.with_luminance(0.1f).red);

    const Color hsv_result = Color(hsv).with_luminance(0.75f);
    EXPECT_TRUE(std::holds_alternative<Hsva>(hsv_result.color));
    EXPECT_NEAR(std::get<Hsva>(hsv_result.color).value,
                Hsva(Oklcha(hsv).with_luminance(0.75f)).value, 1e-5f);

    EXPECT_TRUE(std::holds_alternative<Srgba>(Color(srgb).darker(0.1f).color));
    EXPECT_TRUE(std::holds_alternative<Hsva>(Color(hsv).darker(0.1f).color));
    EXPECT_TRUE(std::holds_alternative<Hsva>(Color(hsv).lighter(0.1f).color));
}

// ===========================================================================
// Color - Hue and Saturation dispatch
// ===========================================================================

TEST(Color, HueAndSaturationDispatch) {
    const Hsla hsl = Hsla::hsl(200.0f, 0.5f, 0.5f);
    // HSL defines both natively.
    EXPECT_FLOAT_EQ(Color(hsl).hue(), 200.0f);
    EXPECT_FLOAT_EQ(Color(hsl).saturation(), 0.5f);

    // sRGB defines neither: hue goes through Oklch, saturation through HSL.
    const Srgba srgb = Srgba::rgb(0.25f, 0.5f, 0.75f);
    EXPECT_FLOAT_EQ(Color(srgb).hue(), Oklcha(srgb).get_hue());
    EXPECT_FLOAT_EQ(Color(srgb).saturation(), Hsla(srgb).get_saturation());

    // The result of with_hue/with_saturation is converted back into the original space, so the
    // variant is preserved and the operation is the chosen space's, wrapped back through the
    // original one.
    EXPECT_TRUE(Color(srgb).with_hue(200.0f) ==
                Color(Srgba(Oklcha(srgb).with_hue(200.0f))));
    EXPECT_TRUE(Color(srgb).with_saturation(0.5f) ==
                Color(Srgba(Hsla(srgb).with_saturation(0.5f))));

    // set_hue keeps the variant, and rotate_hue wraps with rem_euclid.
    Color rotated = Color(srgb);
    rotated.set_hue(0.0f);
    EXPECT_TRUE(std::holds_alternative<Srgba>(rotated.color));
    EXPECT_NEAR(Color(hsl).rotate_hue(370.0f).hue(), 210.0f, 1e-4f);
}

// ===========================================================================
// Color - Mix and EuclideanDistance
// ===========================================================================

TEST(Color, MixAndDistanceAcrossSpaces) {
    const Color red  = Color::srgb(1.0f, 0.0f, 0.0f);
    const Color blue = Color::linear_rgb(0.0f, 0.0f, 1.0f);

    // Bevy mixes in the receiver's own space, so `blue` is converted to sRGB first.
    const Color mixed = red.mix(blue, 0.5f);
    EXPECT_TRUE(std::holds_alternative<Srgba>(mixed.color));
    const Srgba expected =
        Srgba::rgb(1.0f, 0.0f, 0.0f).mix(Srgba(LinearRgba::rgb(0.0f, 0.0f, 1.0f)), 0.5f);
    EXPECT_FLOAT_EQ(std::get<Srgba>(mixed.color).red, expected.red);
    EXPECT_FLOAT_EQ(std::get<Srgba>(mixed.color).green, expected.green);
    EXPECT_FLOAT_EQ(std::get<Srgba>(mixed.color).blue, expected.blue);

    // Alpha takes part in the mix, and mix_assign is `*this = mix(other, factor)`.
    const Color faded = Color::srgb(1.0f, 0.0f, 0.0f).mix(Color::srgba(1.0f, 0.0f, 0.0f, 0.0f), 0.25f);
    EXPECT_FLOAT_EQ(std::get<Srgba>(faded.color).alpha, 0.75f);
    Color assigned = red;
    assigned.mix_assign(blue, 0.5f);
    EXPECT_TRUE(assigned == mixed);

    // HSL has no native distance: both operands go through the chosen space.
    const Color a = Color::hsl(200.0f, 0.5f, 0.5f);
    const Color b = Color::hsl(40.0f, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(a.distance_squared(b),
                    Oklcha(Hsla::hsl(200.0f, 0.5f, 0.5f)).distance_squared(Oklcha(b)));
    EXPECT_NEAR(a.distance(b), std::sqrt(a.distance_squared(b)), 1e-5f);
    EXPECT_GT(a.distance(b), 0.0f);

    // sRGB has one, and alpha is not part of it.
    EXPECT_FLOAT_EQ(Color::srgb(0.0f, 0.0f, 0.0f).distance_squared(Color::srgba(0.0f, 0.0f, 0.0f, 0.0f)), 0.0f);
}

// ===========================================================================
// Color - TryStableInterpolate
// ===========================================================================

TEST(Color, StableInterpolationRejectsMismatchedSpaces) {
    const Color red  = Color::srgb(1.0f, 0.0f, 0.0f);
    const Color blue = Color::srgb(0.0f, 0.0f, 1.0f);

    // The same space mixes, and the result stays in that space.
    const std::expected<Color, MismatchedUnitsError> mixed = red.try_interpolate_stable(blue, 0.5f);
    ASSERT_TRUE(mixed.has_value());
    EXPECT_TRUE(std::holds_alternative<Srgba>(mixed->color));
    EXPECT_FLOAT_EQ(std::get<Srgba>(mixed->color).red, 0.5f);
    EXPECT_TRUE(mixed.value() == red.mix(blue, 0.5f));

    // A different space is a units mismatch; Bevy's error is payload-free.
    const std::expected<Color, MismatchedUnitsError> mismatched =
        red.try_interpolate_stable(Color::linear_rgb(0.0f, 0.0f, 1.0f), 0.5f);
    EXPECT_FALSE(mismatched.has_value());
    static_assert(
        std::is_same_v<std::remove_cvref_t<decltype(mismatched)>::error_type, MismatchedUnitsError>);
    static_assert(sizeof(MismatchedUnitsError) == 1);
}

// ===========================================================================
// palettes::basic - the CSS1 named colors
// ===========================================================================

TEST(ColorPalettes, BasicColors) {
    EXPECT_TRUE(basic::AQUA == Srgba::rgb(0.0f, 1.0f, 1.0f));
    EXPECT_TRUE(basic::BLACK == Srgba::rgb(0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(basic::BLUE == Srgba::rgb(0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(basic::FUCHSIA == Srgba::rgb(1.0f, 0.0f, 1.0f));
    EXPECT_TRUE(basic::GRAY == Srgba::rgb(0.5019608f, 0.5019608f, 0.5019608f));
    EXPECT_TRUE(basic::GREEN == Srgba::rgb(0.0f, 0.5019608f, 0.0f));
    EXPECT_TRUE(basic::LIME == Srgba::rgb(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(basic::MAROON == Srgba::rgb(0.5019608f, 0.0f, 0.0f));
    EXPECT_TRUE(basic::NAVY == Srgba::rgb(0.0f, 0.0f, 0.5019608f));
    EXPECT_TRUE(basic::OLIVE == Srgba::rgb(0.5019608f, 0.5019608f, 0.0f));
    EXPECT_TRUE(basic::PURPLE == Srgba::rgb(0.5019608f, 0.0f, 0.5019608f));
    EXPECT_TRUE(basic::RED == Srgba::rgb(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(basic::SILVER == Srgba::rgb(0.7529412f, 0.7529412f, 0.7529412f));
    EXPECT_TRUE(basic::TEAL == Srgba::rgb(0.0f, 0.5019608f, 0.5019608f));
    EXPECT_TRUE(basic::WHITE == Srgba::rgb(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(basic::YELLOW == Srgba::rgb(1.0f, 1.0f, 0.0f));

    // The constants are fully opaque sRGB colors, and usable as `Color`s.
    EXPECT_FLOAT_EQ(basic::GRAY.alpha, 1.0f);
    EXPECT_TRUE(Color(basic::RED) == Color::srgb(1.0f, 0.0f, 0.0f));
}

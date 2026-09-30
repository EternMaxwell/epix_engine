#include <gtest/gtest.h>
#include <epix/color.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

using namespace epix::color;

namespace {

/** @brief Bevy's `Srgba::hex(..).unwrap()` for the success cases. */
Srgba hex_or_fail(std::string_view hex) {
    const std::expected<Srgba, HexColorError> parsed = Srgba::hex(hex);
    EXPECT_TRUE(parsed.has_value()) << hex;
    return parsed.value_or(Srgba::NONE);
}

/** @brief The error `Srgba::hex(..)` reported; a success is itself a test failure. */
HexColorError hex_error(std::string_view hex) {
    const std::expected<Srgba, HexColorError> parsed = Srgba::hex(hex);
    EXPECT_FALSE(parsed.has_value()) << hex;
    return parsed.error_or(HexColorError{HexColorError::Kind::Length, '\0'});
}

}  // namespace

/** @brief The six standard colors are Bevy's, and `Default` returns `WHITE`
 *  (`bevy_color/src/srgba.rs:47-77`, `:240-244`). */
TEST(Srgba, ConstsAndDefaults) {
    EXPECT_EQ(Srgba::BLACK, Srgba(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(Srgba::NONE, Srgba(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(Srgba::WHITE, Srgba(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_EQ(Srgba::RED, Srgba(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(Srgba::GREEN, Srgba(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(Srgba::BLUE, Srgba(0.0f, 0.0f, 1.0f, 1.0f));

    const Srgba default_color;
    EXPECT_EQ(default_color, Srgba::WHITE);
    EXPECT_FLOAT_EQ(default_color.red, 1.0f);
    EXPECT_FLOAT_EQ(default_color.green, 1.0f);
    EXPECT_FLOAT_EQ(default_color.blue, 1.0f);
    EXPECT_FLOAT_EQ(default_color.alpha, 1.0f);

    EXPECT_EQ(Srgba::BLACK.with_red(1.0f), Srgba::RED);
    EXPECT_EQ(Srgba::BLACK.with_green(1.0f), Srgba::GREEN);
    EXPECT_EQ(Srgba::BLACK.with_blue(1.0f), Srgba::BLUE);
    EXPECT_FLOAT_EQ(Srgba::BLACK.with_red(0.25f).red, 0.25f);

    // `Alpha` spells its accessor `get_alpha` because the field keeps Bevy's name.
    const Srgba translucent = Srgba::WHITE.with_alpha(0.5f);
    EXPECT_FLOAT_EQ(translucent.get_alpha(), 0.5f);
    EXPECT_FALSE(translucent.is_fully_opaque());
    EXPECT_FALSE(translucent.is_fully_transparent());
    EXPECT_TRUE(Srgba::NONE.is_fully_transparent());
    EXPECT_TRUE(Srgba::WHITE.is_fully_opaque());

    Srgba mutable_color = Srgba::NONE;
    mutable_color.set_alpha(1.0f);
    EXPECT_FLOAT_EQ(mutable_color.alpha, 1.0f);
    EXPECT_TRUE(mutable_color.is_fully_opaque());
}

/** @brief `new`/`rgb` and the packed `u8` constructors and accessors
 *  (`bevy_color/src/srgba.rs:79-110`, `:185-212`, `:373-389`). */
TEST(Srgba, ConstructorsAndPackedU8) {
    const Srgba rgb = Srgba::rgb(0.25f, 0.5f, 0.75f);
    EXPECT_FLOAT_EQ(rgb.red, 0.25f);
    EXPECT_FLOAT_EQ(rgb.green, 0.5f);
    EXPECT_FLOAT_EQ(rgb.blue, 0.75f);
    EXPECT_FLOAT_EQ(rgb.alpha, 1.0f);

    EXPECT_EQ(Srgba::rgb_u8(255, 255, 255), Srgba::WHITE);
    EXPECT_EQ(Srgba::rgba_u8(0, 0, 0, 0), Srgba::NONE);
    EXPECT_FLOAT_EQ(Srgba::rgb_u8(51, 0, 0).red, 51.0f / 255.0f);
    EXPECT_FLOAT_EQ(Srgba::rgba_u8(0, 0, 0, 128).alpha, 128.0f / 255.0f);

    const std::array<std::uint8_t, 4> packed = Srgba::rgb_u8(3, 169, 244).u8_array();
    EXPECT_EQ(static_cast<int>(packed[0]), 3);
    EXPECT_EQ(static_cast<int>(packed[1]), 169);
    EXPECT_EQ(static_cast<int>(packed[2]), 244);
    EXPECT_EQ(static_cast<int>(packed[3]), 255);
    EXPECT_EQ(Srgba(packed), Srgba::rgb_u8(3, 169, 244));
    EXPECT_EQ(Srgba(std::array<std::uint8_t, 3>{3, 169, 244}), Srgba::rgb_u8(3, 169, 244));

    const std::array<std::uint8_t, 3> packed_rgb = Srgba::BLUE.u8_array_no_alpha();
    EXPECT_EQ(static_cast<int>(packed_rgb[0]), 0);
    EXPECT_EQ(static_cast<int>(packed_rgb[2]), 255);

    // Every channel is clamped to [0.0, 1.0] and rounded before the cast, so out-of-range
    // channels saturate and 0.5 lands on 128 (`srgba.rs:376`).
    const std::array<std::uint8_t, 4> clamped = Srgba(-1.0f, 2.0f, 0.5f, 1.0f).u8_array();
    EXPECT_EQ(static_cast<int>(clamped[0]), 0);
    EXPECT_EQ(static_cast<int>(clamped[1]), 255);
    EXPECT_EQ(static_cast<int>(clamped[2]), 128);
}

/** @brief Bevy's own `hex_color` assertions (`bevy_color/src/srgba.rs:498-520`), plus the error
 *  kinds the port reports. */
TEST(Srgba, HexParse) {
    EXPECT_EQ(hex_or_fail("FFF"), Srgba::WHITE);
    EXPECT_EQ(hex_or_fail("FFFF"), Srgba::WHITE);
    EXPECT_EQ(hex_or_fail("FFFFFF"), Srgba::WHITE);
    EXPECT_EQ(hex_or_fail("FFFFFFFF"), Srgba::WHITE);
    EXPECT_EQ(hex_or_fail("000"), Srgba::BLACK);
    EXPECT_EQ(hex_or_fail("000F"), Srgba::BLACK);
    EXPECT_EQ(hex_or_fail("000000"), Srgba::BLACK);
    EXPECT_EQ(hex_or_fail("000000FF"), Srgba::BLACK);
    EXPECT_EQ(hex_or_fail("03a9f4"), Srgba::rgb_u8(3, 169, 244));
    EXPECT_EQ(hex_or_fail("#f2a"), Srgba::rgb_u8(255, 34, 170));
    EXPECT_EQ(hex_or_fail("#e23030"), Srgba::rgb_u8(226, 48, 48));
    EXPECT_EQ(hex_or_fail("11223344"), Srgba::rgba_u8(17, 34, 51, 68));
    EXPECT_EQ(hex_or_fail("1234"), Srgba::rgba_u8(17, 34, 51, 68));
    EXPECT_EQ(hex_or_fail("12345678"), Srgba::rgba_u8(18, 52, 86, 120));
    EXPECT_EQ(hex_or_fail("4321"), Srgba::rgba_u8(68, 51, 34, 17));

    // Length is matched first, so any other digit count is `Length` whatever the characters are.
    EXPECT_TRUE(hex_error("yy").kind == HexColorError::Kind::Length);
    EXPECT_TRUE(hex_error("#ff").kind == HexColorError::Kind::Length);
    EXPECT_TRUE(hex_error("").kind == HexColorError::Kind::Length);

    // Bevy reports these two as `Parse(ParseIntError)` because `from_str_radix` is what finds the
    // bad character; with no `ParseIntError` to carry, the port reports the character itself.
    EXPECT_TRUE(hex_error("yyy").kind == HexColorError::Kind::Char);
    EXPECT_EQ(hex_error("yyy").value, 'y');
    EXPECT_TRUE(hex_error("##fff").kind == HexColorError::Kind::Char);
    EXPECT_EQ(hex_error("##fff").value, '#');
    EXPECT_TRUE(hex_error("12345z").kind == HexColorError::Kind::Char);
}

/** @brief The `hex()` accessor emits `#RRGGBB` for opaque colors and `#RRGGBBAA` otherwise, and
 *  the static `hex(std::string_view)` parser reads both back (`bevy_color/src/srgba.rs:175-183`). */
TEST(Srgba, HexRoundTrip) {
    EXPECT_EQ(Srgba::WHITE.hex(), "#FFFFFF");
    EXPECT_EQ(Srgba::BLACK.hex(), "#000000");
    EXPECT_EQ(Srgba::NONE.hex(), "#00000000");
    EXPECT_EQ(Srgba::rgb_u8(3, 169, 244).hex(), "#03A9F4");

    const Srgba translucent = Srgba(1.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_EQ(translucent.hex(), "#FF000080");

    for (const Srgba& color : {Srgba::WHITE, Srgba::BLACK, Srgba::NONE, translucent}) {
        EXPECT_EQ(hex_or_fail(color.hex()), Srgba(color.u8_array()));
        EXPECT_EQ(hex_or_fail(color.hex()).hex(), color.hex());
    }
}

/** @brief Both gamma functions at their branch boundaries and on either side of them
 *  (`bevy_color/src/srgba.rs:214-237`). */
TEST(Srgba, GammaFunctionBranches) {
    // Values at or below zero are returned unchanged, before any branch.
    EXPECT_FLOAT_EQ(Srgba::gamma_function(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(Srgba::gamma_function(-0.5f), -0.5f);
    EXPECT_FLOAT_EQ(Srgba::gamma_function_inverse(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(Srgba::gamma_function_inverse(-1.0f), -1.0f);

    // At the boundary the linear falloff still applies...
    EXPECT_NEAR(Srgba::gamma_function(0.04045f), 0.04045f / 12.92f, 1e-7f);
    EXPECT_NEAR(Srgba::gamma_function_inverse(0.0031308f), 0.0031308f * 12.92f, 1e-7f);

    // ...and just above it the gamma curve does.
    EXPECT_NEAR(Srgba::gamma_function(0.040451f), std::pow((0.040451f + 0.055f) / 1.055f, 2.4f), 1e-7f);
    EXPECT_NEAR(Srgba::gamma_function_inverse(0.0031309f),
                1.055f * std::pow(0.0031309f, 1.0f / 2.4f) - 0.055f, 1e-7f);

    // Bevy's own worked value, and the two functions as a round trip.
    EXPECT_NEAR(Srgba::gamma_function(0.5f), 0.2140f, 0.0001f);
    EXPECT_NEAR(Srgba::gamma_function_inverse(Srgba::gamma_function(0.5f)), 0.5f, 1e-5f);
}

/** @brief Bevy's `test_to_from_linear` (`bevy_color/src/srgba.rs:450-463`): the sRGB transfer
 *  function on RGB, the alpha copied straight across. */
TEST(Srgba, LinearRgbaRoundTrip) {
    const Srgba srgba         = Srgba(0.0f, 0.5f, 1.0f, 1.0f);
    const LinearRgba linear   = LinearRgba(srgba);
    EXPECT_FLOAT_EQ(linear.red, 0.0f);
    EXPECT_NEAR(linear.green, 0.2140f, 0.0001f);
    EXPECT_NEAR(linear.blue, 1.0f, 0.0001f);
    EXPECT_FLOAT_EQ(linear.alpha, 1.0f);

    const Srgba srgba2 = Srgba(linear);
    EXPECT_FLOAT_EQ(srgba2.red, 0.0f);
    EXPECT_NEAR(srgba2.green, 0.5f, 0.0001f);
    EXPECT_NEAR(srgba2.blue, 1.0f, 0.0001f);
    EXPECT_FLOAT_EQ(srgba2.alpha, 1.0f);

    // Alpha is never gamma corrected.
    const Srgba translucent = Srgba(0.25f, 0.5f, 0.75f, 0.25f);
    EXPECT_FLOAT_EQ(LinearRgba(translucent).alpha, 0.25f);
    EXPECT_FLOAT_EQ(Srgba(LinearRgba(translucent)).alpha, 0.25f);
}

/** @brief Bevy's `euclidean_distance` (`bevy_color/src/srgba.rs:465-481`) plus the `Mix` and
 *  `Gray` members, which blend non-linear sRGB channels directly (`:274-285`, `:314-317`). */
TEST(Srgba, EuclideanDistanceAndMix) {
    // White to black.
    EXPECT_FLOAT_EQ(Srgba(0.0f, 0.0f, 0.0f, 1.0f).distance_squared(Srgba(1.0f, 1.0f, 1.0f, 1.0f)), 3.0f);
    // Alpha shouldn't matter.
    EXPECT_FLOAT_EQ(Srgba(0.0f, 0.0f, 0.0f, 1.0f).distance_squared(Srgba(1.0f, 1.0f, 1.0f, 0.0f)), 3.0f);
    // Red to green.
    EXPECT_FLOAT_EQ(Srgba(0.0f, 0.0f, 0.0f, 1.0f).distance_squared(Srgba(1.0f, 0.0f, 0.0f, 1.0f)), 1.0f);
    EXPECT_NEAR(Srgba::BLACK.distance(Srgba::WHITE), std::sqrt(3.0f), 1e-5f);

    const Srgba mid = Srgba::BLACK.mix(Srgba::WHITE, 0.5f);
    EXPECT_FLOAT_EQ(mid.red, 0.5f);
    EXPECT_FLOAT_EQ(mid.green, 0.5f);
    EXPECT_FLOAT_EQ(mid.blue, 0.5f);
    EXPECT_FLOAT_EQ(mid.alpha, 1.0f);
    EXPECT_EQ(Srgba::gray(0.5f), mid);
    EXPECT_EQ(Srgba::gray(0.0f), Srgba::BLACK);

    Srgba assigned = Srgba::BLACK;
    assigned.mix_assign(Srgba::WHITE, 0.25f);
    EXPECT_EQ(assigned, Srgba::gray(0.25f));

    // Alpha takes part in the mix, so a mix can be partially transparent.
    EXPECT_FLOAT_EQ(Srgba::NONE.mix(Srgba::WHITE, 0.25f).alpha, 0.25f);
}

/** @brief Bevy's `darker_lighter` (`bevy_color/src/srgba.rs:483-496`), which goes through the
 *  linear space, and `luminance` on the linear equivalent (`:246-251`). */
TEST(Srgba, DarkerLighter) {
    const Srgba color = Srgba(0.4f, 0.5f, 0.6f, 1.0f);

    // Darker and lighter should be commutative.
    const Srgba darker1       = color.darker(0.1f);
    const Srgba darker2       = darker1.darker(0.1f);
    const Srgba twice_as_dark = color.darker(0.2f);
    EXPECT_LT(darker2.distance_squared(twice_as_dark), 0.0001f);

    const Srgba lighter1       = color.lighter(0.1f);
    const Srgba lighter2       = lighter1.lighter(0.1f);
    const Srgba twice_as_light = color.lighter(0.2f);
    EXPECT_LT(lighter2.distance_squared(twice_as_light), 0.0001f);

    EXPECT_NEAR(Srgba::WHITE.luminance(), 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(Srgba::BLACK.luminance(), 0.0f);

    // `with_luminance` hands `gamma_function(luminance)` to `LinearRgba::with_luminance`, so the
    // requested value is gamma-corrected on the way in and the result is not a fixed point of
    // `luminance()`; only its direction and the untouched alpha are asserted here.
    const Srgba dim = Srgba::WHITE.with_luminance(0.5f);
    EXPECT_LT(dim.luminance(), Srgba::WHITE.luminance());
    EXPECT_FLOAT_EQ(dim.alpha, 1.0f);
    EXPECT_NEAR(Srgba::WHITE.with_luminance(1.0f).luminance(), 1.0f, 1e-4f);
}

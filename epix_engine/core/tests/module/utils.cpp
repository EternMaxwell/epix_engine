#include <gtest/gtest.h>

import epix.utils;

using namespace epix;

namespace {
constexpr float kFixed32Tolerance  = 1e-4f;
constexpr double kFixed64Tolerance = 1e-9;
}  // namespace

TEST(FixedPointTest, BasicOperations) {
    using fixed16_16 = utils::fixed32<16>;
    fixed16_16 a(1.5f);
    fixed16_16 b(2.25f);

    EXPECT_NEAR(static_cast<float>(a + b), 3.75f, kFixed32Tolerance);
    EXPECT_NEAR(static_cast<float>(a - b), -0.75f, kFixed32Tolerance);
    EXPECT_NEAR(static_cast<float>(a * b), 3.375f, kFixed32Tolerance);
    EXPECT_NEAR(static_cast<float>(a / b), 0.6666667f, kFixed32Tolerance);
}

TEST(FixedPointTest, NegativeValues) {
    using fixed16_16 = utils::fixed32<16>;
    fixed16_16 a(-1.5f);
    fixed16_16 b(2.25f);

    EXPECT_NEAR(static_cast<float>(a + b), 0.75f, kFixed32Tolerance);
    EXPECT_NEAR(static_cast<float>(a - b), -3.75f, kFixed32Tolerance);
    EXPECT_NEAR(static_cast<float>(a * b), -3.375f, kFixed32Tolerance);
    EXPECT_NEAR(static_cast<float>(a / b), -0.6666667f, kFixed32Tolerance);
}

TEST(FixedPointTest, Fixed64Operations) {
    using fixed32_32 = utils::fixed64<32>;
    fixed32_32 a(1.5);
    fixed32_32 b(2.25);

    EXPECT_NEAR(static_cast<double>(a + b), 3.75, kFixed64Tolerance);
    EXPECT_NEAR(static_cast<double>(a - b), -0.75, kFixed64Tolerance);
    EXPECT_NEAR(static_cast<double>(a * b), 3.375, kFixed64Tolerance);
    EXPECT_NEAR(static_cast<double>(a / b), 0.6666666666666666, kFixed64Tolerance);
}

TEST(FixedPointTest, Fixed64NegativeValues) {
    using fixed32_32 = utils::fixed64<32>;
    fixed32_32 a(-1.5);
    fixed32_32 b(2.25);

    EXPECT_NEAR(static_cast<double>(a + b), 0.75, kFixed64Tolerance);
    EXPECT_NEAR(static_cast<double>(a - b), -3.75, kFixed64Tolerance);
    EXPECT_NEAR(static_cast<double>(a * b), -3.375, kFixed64Tolerance);
    EXPECT_NEAR(static_cast<double>(a / b), -0.6666666666666666, kFixed64Tolerance);
}
#include <gtest/gtest.h>

import epix.utils;

TEST(FixedPointTest, BasicOperations) {
    using fixed16_16 = utils::fixed32<16>;
    fixed16_16 a(1.5f);
    fixed16_16 b(2.25f);

    EXPECT_FLOAT_EQ(static_cast<float>(a + b), 3.75f);
    EXPECT_FLOAT_EQ(static_cast<float>(a - b), -0.75f);
    EXPECT_FLOAT_EQ(static_cast<float>(a * b), 3.375f);
    EXPECT_FLOAT_EQ(static_cast<float>(a / b), 0.6666667f);
}

TEST(FixedPointTest, NegativeValues) {
    using fixed16_16 = utils::fixed32<16>;
    fixed16_16 a(-1.5f);
    fixed16_16 b(2.25f);

    EXPECT_FLOAT_EQ(static_cast<float>(a + b), 0.75f);
    EXPECT_FLOAT_EQ(static_cast<float>(a - b), -3.75f);
    EXPECT_FLOAT_EQ(static_cast<float>(a * b), -3.375f);
    EXPECT_FLOAT_EQ(static_cast<float>(a / b), -0.6666667f);
}

TEST(FixedPointTest, Fixed64Operations) {
    using fixed32_32 = utils::fixed64<32>;
    fixed32_32 a(1.5);
    fixed32_32 b(2.25);

    EXPECT_DOUBLE_EQ(static_cast<double>(a + b), 3.75);
    EXPECT_DOUBLE_EQ(static_cast<double>(a - b), -0.75);
    EXPECT_DOUBLE_EQ(static_cast<double>(a * b), 3.375);
    EXPECT_DOUBLE_EQ(static_cast<double>(a / b), 0.6666666666666666);
}

TEST(FixedPointTest, Fixed64NegativeValues) {
    using fixed32_32 = utils::fixed64<32>;
    fixed32_32 a(-1.5);
    fixed32_32 b(2.25);

    EXPECT_DOUBLE_EQ(static_cast<double>(a + b), 0.75);
    EXPECT_DOUBLE_EQ(static_cast<double>(a - b), -3.75);
    EXPECT_DOUBLE_EQ(static_cast<double>(a * b), -3.375);
    EXPECT_DOUBLE_EQ(static_cast<double>(a / b), -0.6666666666666666);
}
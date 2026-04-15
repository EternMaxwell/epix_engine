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

TEST(BroadcastChannelTest, DeliversEachMessageToEachReceiver) {
    auto [sender, receiver_a] = utils::make_broadcast_channel<int>();
    auto receiver_b           = receiver_a;

    sender.send(7);
    sender.send(8);

    auto first_a = receiver_a.receive();
    ASSERT_TRUE(first_a.has_value());
    EXPECT_EQ(*first_a, 7);

    auto second_a = receiver_a.receive();
    ASSERT_TRUE(second_a.has_value());
    EXPECT_EQ(*second_a, 8);

    auto first_b = receiver_b.receive();
    ASSERT_TRUE(first_b.has_value());
    EXPECT_EQ(*first_b, 7);

    auto second_b = receiver_b.receive();
    ASSERT_TRUE(second_b.has_value());
    EXPECT_EQ(*second_b, 8);
}

TEST(BroadcastChannelTest, ReturnsClosedAfterLastSenderDrops) {
    auto receiver = [] {
        auto [sender, inner_receiver] = utils::make_broadcast_channel<int>();
        auto sender_copy              = sender;

        sender.send(42);
        auto value = inner_receiver.receive();
        EXPECT_TRUE(value.has_value());
        EXPECT_EQ(*value, 42);

        sender_copy = {};
        return inner_receiver;
    }();

    auto result = receiver.try_receive();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), utils::ReceiveError::Closed);
}

TEST(ChannelTest, CloseWakesBlockedReceiver) {
    auto [sender, receiver] = utils::make_channel<int>();
    sender.close();

    auto result = receiver.receive();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), utils::ReceiveError::Closed);
}

TEST(BroadcastChannelTest, CloseWakesBlockedReceiver) {
    auto [sender, receiver] = utils::make_broadcast_channel<int>();
    sender.close();

    auto result = receiver.receive();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), utils::ReceiveError::Closed);
}
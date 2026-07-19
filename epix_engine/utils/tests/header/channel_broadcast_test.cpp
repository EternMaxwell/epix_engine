#include <gtest/gtest.h>

#include <epix/async_broadcast.hpp>
#include <epix/async_channel.hpp>
#include <optional>
#include <stdexec/execution.hpp>
#include <tuple>

TEST(AsyncChannelStdexecTask, SendRecv) {
    auto [tx, rx] = epix::async_channel::bounded<int>(1);

    auto sent = STDEXEC::sync_wait(tx.send(42));
    ASSERT_TRUE(sent.has_value());
    EXPECT_TRUE(std::get<0>(*sent).has_value());

    auto received = STDEXEC::sync_wait(rx.recv());
    ASSERT_TRUE(received.has_value());
    ASSERT_TRUE(std::get<0>(*received).has_value());
    EXPECT_EQ(std::get<0>(*received).value(), 42);
}

TEST(AsyncBroadcastStdexecTask, BroadcastRecv) {
    auto [tx, rx1] = epix::async_broadcast::broadcast<int>(2);
    epix::async_broadcast::Receiver<int> rx2;
    {
        SCOPED_TRACE("new_receiver");
        rx2 = tx.new_receiver();
    }

    std::optional<std::tuple<std::expected<void, epix::async_broadcast::SendError<int>>>> sent;
    {
        SCOPED_TRACE("broadcast");
        sent = STDEXEC::sync_wait(tx.broadcast(7));
    }
    ASSERT_TRUE(sent.has_value());
    EXPECT_TRUE(std::get<0>(*sent).has_value());

    std::optional<std::tuple<std::expected<int, epix::async_broadcast::RecvError>>> received1;
    {
        SCOPED_TRACE("recv1");
        received1 = STDEXEC::sync_wait(rx1.recv());
    }
    ASSERT_TRUE(received1.has_value());
    ASSERT_TRUE(std::get<0>(*received1).has_value());
    EXPECT_EQ(std::get<0>(*received1).value(), 7);

    std::optional<std::tuple<std::expected<int, epix::async_broadcast::RecvError>>> received2;
    {
        SCOPED_TRACE("recv2");
        received2 = STDEXEC::sync_wait(rx2.recv());
    }
    ASSERT_TRUE(received2.has_value());
    ASSERT_TRUE(std::get<0>(*received2).has_value());
    EXPECT_EQ(std::get<0>(*received2).value(), 7);
}

TEST(AsyncBroadcastStdexecTask, BroadcastAllowsInactiveReceivers) {
    auto [tx, rx] = epix::async_broadcast::broadcast<int>(1);
    auto inactive = rx.deactivate();
    (void) inactive;

    auto sent = STDEXEC::sync_wait(tx.broadcast(11));
    ASSERT_TRUE(sent.has_value());
    EXPECT_TRUE(std::get<0>(*sent).has_value());
    EXPECT_EQ(tx.len(), 1u);
}

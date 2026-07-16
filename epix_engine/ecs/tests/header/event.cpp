#include <gtest/gtest.h>

#include <epix/ecs.hpp>

using namespace epix::ecs;

TEST(ecs_event, retains_events_for_one_update_cycle) {
    Events<int> events;
    events.push(3);
    events.emplace(5);

    EXPECT_EQ(events.head(), 0);
    EXPECT_EQ(events.tail(), 2);
    EXPECT_EQ(events.size(), 2);
    EXPECT_EQ(*events.get(0), 3);
    EXPECT_EQ(*events.get(1), 5);

    events.update();
    EXPECT_EQ(events.size(), 2);
    events.update();

    EXPECT_TRUE(events.empty());
    EXPECT_EQ(events.head(), 2);
    EXPECT_EQ(events.tail(), 2);
}

TEST(ecs_event, advance_head_discards_only_the_requested_prefix) {
    Events<int> events;
    events.push(1);
    events.push(2);
    events.push(3);

    events.advance_head(2);

    EXPECT_EQ(events.head(), 2);
    EXPECT_EQ(events.tail(), 3);
    ASSERT_NE(events.get(2), nullptr);
    EXPECT_EQ(*events.get(2), 3);
}

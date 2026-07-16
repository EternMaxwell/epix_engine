#include <gtest/gtest.h>

import epix.ecs;

using namespace epix::ecs;

TEST(ecs_event, module_exposes_event_lifetime) {
    Events<int> events;
    events.push(42);
    events.update();
    EXPECT_FALSE(events.empty());
    events.update();
    EXPECT_TRUE(events.empty());
}

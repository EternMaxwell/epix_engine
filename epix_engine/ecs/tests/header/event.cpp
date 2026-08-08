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

TEST(ecs_event, registry_updates_entity_backed_event_resources) {
    World world(WorldId(20));
    EventRegistry::register_event<int>(world);

    auto entity = world.resource_entity<Events<int>>();
    ASSERT_TRUE(entity);
    EXPECT_TRUE(world.entity(*entity).contains<IsResource>());

    world.resource_mut<Events<int>>().push(7);
    auto& registry = world.resource_mut<EventRegistry>();
    registry.run_updates(world, Tick(0));
    EXPECT_EQ(world.resource<Events<int>>().size(), 1);

    world.increment_change_tick();
    registry.run_updates(world, Tick(1));
    EXPECT_TRUE(world.resource<Events<int>>().empty());
}

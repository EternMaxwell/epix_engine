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

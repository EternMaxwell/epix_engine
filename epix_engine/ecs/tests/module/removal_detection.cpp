#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <iterator>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;

using namespace epix::ecs;

namespace {
struct Position {
    int value = 0;
};
struct Velocity {
    int value = 0;
};
struct SparseTag {};
struct HookedRemoval {
    static inline bool event_visible_during_hook = false;

    static void on_remove(World& world, HookContext) {
        event_visible_during_hook = !std::ranges::empty(world.removed<HookedRemoval>());
    }
};
}  // namespace

template <>
struct epix::ecs::sparse_component<SparseTag> : std::true_type {};

TEST(ecs, removed_components_records_remove_and_despawn_but_not_replace) {
    World world(1);
    Entity removed_entity   = world.spawn(Position{1}, SparseTag{}).id();
    Entity despawned_entity = world.spawn(Position{2}, Velocity{}).id();

    world.entity_mut(removed_entity).insert(Position{3});
    EXPECT_TRUE(std::ranges::empty(world.removed<Position>()));

    world.entity_mut(removed_entity).remove<Position, SparseTag>();
    world.entity_mut(despawned_entity).despawn();

    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed<Position>()),
              (std::vector{removed_entity, despawned_entity}));
    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed<SparseTag>()), (std::vector{removed_entity}));
    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed<Velocity>()), (std::vector{despawned_entity}));
}

TEST(ecs, removed_components_readers_are_independent_and_retained_for_two_clears) {
    World world(2);
    Entity entity = world.spawn(Position{}).id();

    std::vector<Entity> first_reads;
    std::vector<Entity> second_reads;
    auto first  = make_system_unique([&](RemovedComponents<Position> removed) {
        std::ranges::copy(removed.read(), std::back_inserter(first_reads));
    });
    auto second = make_system_unique([&](RemovedComponents<Position> removed) {
        std::ranges::copy(removed.read(), std::back_inserter(second_reads));
    });
    first->initialize(world);
    second->initialize(world);

    world.entity_mut(entity).remove<Position>();
    ASSERT_TRUE(first->run({}, world).has_value());
    ASSERT_TRUE(first->run({}, world).has_value());
    EXPECT_EQ(first_reads, (std::vector{entity}));

    world.clear_trackers();
    EXPECT_TRUE(std::ranges::empty(world.removed<Position>()));
    ASSERT_TRUE(second->run({}, world).has_value());
    EXPECT_EQ(second_reads, (std::vector{entity}));

    world.clear_trackers();
    auto expired = make_system_unique([&](RemovedComponents<Position> removed) { EXPECT_TRUE(removed.is_empty()); });
    expired->initialize(world);
    EXPECT_TRUE(expired->run({}, world).has_value());
}

TEST(ecs, removed_components_supports_runtime_component_ids) {
    World world(3);
    Entity entity      = world.spawn(Velocity{}).id();
    TypeId velocity_id = world.components().get_valid_id<Velocity>().value();

    EXPECT_TRUE(world.entity_mut(entity).remove_by_id(velocity_id));
    EXPECT_FALSE(world.entity_mut(entity).remove_by_id(velocity_id));
    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed_with_id(velocity_id)), (std::vector{entity}));
}

TEST(ecs, removed_components_only_reports_bundle_members_that_were_present) {
    World world(4);
    Entity entity = world.spawn(Position{}).id();

    world.entity_mut(entity).remove<Position, Velocity>();

    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed<Position>()), (std::vector{entity}));
    EXPECT_TRUE(std::ranges::empty(world.removed<Velocity>()));
}

TEST(ecs, removed_components_tracks_resource_removal) {
    World world(5);
    world.insert_resource(Position{7});

    auto value = world.take_resource<Position>();

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->value, 7);
    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed<Position>()).size(), 1);
}

TEST(ecs, removed_components_exposes_bevy_reader_api) {
    World world(6);
    Entity first_entity  = world.spawn(Position{}).id();
    Entity second_entity = world.spawn(Position{}).id();

    world.entity_mut(first_entity).remove<Position>();
    world.entity_mut(second_entity).remove<Position>();

    auto reader_system = make_system_unique([&](RemovedComponents<Position> removed) {
        EXPECT_EQ(removed.len(), 2);
        EXPECT_EQ(removed.size(), 2);
        EXPECT_FALSE(removed.is_empty());
        EXPECT_FALSE(removed.empty());
        EXPECT_TRUE(removed.events().has_value());
        (void)removed.reader();
        (void)removed.reader_mut();

        auto events = std::ranges::to<std::vector<std::tuple<Entity, std::uint32_t>>>(removed.read_with_id());
        ASSERT_EQ(events.size(), 2);
        EXPECT_EQ(std::get<0>(events[0]), first_entity);
        EXPECT_EQ(std::get<0>(events[1]), second_entity);
        EXPECT_LT(std::get<1>(events[0]), std::get<1>(events[1]));
        EXPECT_TRUE(removed.is_empty());
    });
    reader_system->initialize(world);
    ASSERT_TRUE(reader_system->run({}, world).has_value());

    auto clear_system = make_system_unique([](RemovedComponents<Position> removed) {
        EXPECT_EQ(removed.len(), 2);
        removed.clear();
        EXPECT_TRUE(removed.is_empty());
    });
    clear_system->initialize(world);
    EXPECT_TRUE(clear_system->run({}, world).has_value());
}

TEST(ecs, removed_components_is_recorded_after_remove_hooks) {
    World world(7);
    HookedRemoval::event_visible_during_hook = false;
    Entity entity                            = world.spawn(HookedRemoval{}).id();

    world.entity_mut(entity).remove<HookedRemoval>();

    EXPECT_FALSE(HookedRemoval::event_visible_during_hook);
    EXPECT_EQ(std::ranges::to<std::vector<Entity>>(world.removed<HookedRemoval>()), (std::vector{entity}));
}

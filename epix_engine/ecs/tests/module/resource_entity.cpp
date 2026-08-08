#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <stdexcept>
#include <type_traits>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;

using namespace epix::ecs;

struct SparseMovableResource {
    int value = 0;
};
template <>
struct epix::ecs::sparse_component<SparseMovableResource> : std::true_type {};

namespace {
struct MovableResource {
    int value = 0;
};

struct OrdinaryComponent {
    int value = 0;
};

struct ImmovableResource {
    explicit ImmovableResource(int value) : value(value) {}
    ImmovableResource(const ImmovableResource&)            = delete;
    ImmovableResource(ImmovableResource&&)                 = delete;
    ImmovableResource& operator=(const ImmovableResource&) = delete;
    ImmovableResource& operator=(ImmovableResource&&)      = delete;

    int value;
};

void conflicting_resource_access(ResMut<MovableResource>, Query<const MovableResource&>) {}
void read_movable_resource(Res<MovableResource>) {}
void insert_resources_deferred(Commands commands) {
    commands.insert_resource(MovableResource{13});
    commands.emplace_resource<ImmovableResource>(17);
}
void remove_resources_deferred(Commands commands) {
    commands.remove_resource<MovableResource>();
    commands.remove_resource<ImmovableResource>();
}
}  // namespace

static_assert(std::movable<MovableResource>);
static_assert(!std::movable<ImmovableResource>);

TEST(ecs, movable_resource_is_an_entity_component) {
    World world(WorldId(1));
    world.insert_resource(MovableResource{1});

    auto resource_id     = world.components().get_valid_id<MovableResource>().value();
    auto resource_entity = world.resource_entity<MovableResource>();
    ASSERT_TRUE(resource_entity);
    EXPECT_EQ(world.resource_entities().get(resource_id), resource_entity);

    auto entity = world.entity(*resource_entity);
    ASSERT_TRUE(entity.contains<MovableResource>());
    ASSERT_TRUE(entity.contains<IsResource>());
    EXPECT_EQ(entity.get<IsResource>()->get().resource_component_id(), resource_id);

    world.entity_mut(*resource_entity).get_mut<MovableResource>()->get_mut().value = 7;
    EXPECT_EQ(world.resource<MovableResource>().value, 7);

    world.entity_mut(*resource_entity).remove<MovableResource>();
    EXPECT_FALSE(world.get_resource<MovableResource>());
    EXPECT_EQ(world.resource_entity<MovableResource>(), resource_entity);

    world.entity_mut(*resource_entity).insert(MovableResource{8});
    EXPECT_EQ(world.resource<MovableResource>().value, 8);
    EXPECT_TRUE(world.remove_resource<MovableResource>());

    world.insert_resource(MovableResource{9});
    EXPECT_EQ(world.resource_entity<MovableResource>(), resource_entity);
    EXPECT_EQ(world.resource<MovableResource>().value, 9);
}

TEST(ecs, system_resource_registration_configures_is_resource) {
    World world(WorldId(6));
    auto system = make_system_unique(read_movable_resource);
    system->initialize(world);

    auto resource_entity = world.spawn(MovableResource{21}).id();
    EXPECT_EQ(world.resource_entity<MovableResource>(), resource_entity);
    EXPECT_TRUE(world.entity(resource_entity).contains<IsResource>());
    EXPECT_EQ(world.resource<MovableResource>().value, 21);
}

TEST(ecs, queued_resource_registration_configures_is_resource) {
    World world(WorldId(9));
    auto resource_id = world.queued_registrator().queue_register_resource<MovableResource>();
    world.flush_components();

    auto resource_entity = world.spawn(MovableResource{34}).id();
    EXPECT_EQ(world.resource_entities().get(resource_id), resource_entity);
    EXPECT_TRUE(world.entity(resource_entity).contains<IsResource>());
    EXPECT_EQ(world.resource<MovableResource>().value, 34);
}

TEST(ecs, direct_entity_mutation_is_visible_to_resource_change_detection) {
    World world(WorldId(7));
    world.insert_resource(MovableResource{1});
    bool modified = false;
    auto system   = make_system_unique([&](Res<MovableResource> resource) { modified = resource.is_modified(); });
    system->initialize(world);

    ASSERT_TRUE(system->run({}, world));
    EXPECT_TRUE(modified);
    ASSERT_TRUE(system->run({}, world));
    EXPECT_FALSE(modified);

    world.increment_change_tick();
    auto entity = world.resource_entity<MovableResource>().value();
    world.entity_mut(entity).get_mut<MovableResource>()->get_mut().value = 2;
    ASSERT_TRUE(system->run({}, world));
    EXPECT_TRUE(modified);
    EXPECT_EQ(world.resource<MovableResource>().value, 2);
}

TEST(ecs, is_resource_enforces_singleton_and_removal_invariants) {
    World world(WorldId(2));
    world.insert_resource(MovableResource{1});
    auto resource_id = world.components().get_valid_id<MovableResource>().value();
    auto canonical   = world.resource_entity<MovableResource>().value();

    auto duplicate = world.spawn(MovableResource{2}).id();
    EXPECT_FALSE(world.entity(duplicate).contains<MovableResource>());
    EXPECT_FALSE(world.entity(duplicate).contains<IsResource>());
    EXPECT_EQ(world.resource_entity<MovableResource>(), canonical);
    EXPECT_EQ(world.resource<MovableResource>().value, 1);

    world.entity_mut(canonical).remove<IsResource>();
    EXPECT_FALSE(world.resource_entity<MovableResource>());
    EXPECT_FALSE(world.entity(canonical).contains<MovableResource>());

    world.insert_resource(MovableResource{3});
    auto replacement = world.resource_entity<MovableResource>();
    ASSERT_TRUE(replacement);
    EXPECT_NE(*replacement, canonical);
    EXPECT_EQ(world.resource_entities().get(resource_id), replacement);

    world.entity_mut(*replacement).despawn();
    EXPECT_FALSE(world.resource_entity<MovableResource>());
    EXPECT_FALSE(world.get_resource<MovableResource>());
}

TEST(ecs, component_cannot_become_a_resource_after_entity_use) {
    World world(WorldId(8));
    world.spawn(MovableResource{1});
    EXPECT_THROW(world.register_resource<MovableResource>(), std::logic_error);
}

TEST(ecs, invalid_is_resource_marker_is_removed_through_entity_access) {
    World world(WorldId(10));
    world.insert_resource(MovableResource{1});
    auto entity      = world.resource_entity<MovableResource>().value();
    auto ordinary_id = world.registrator().register_component<OrdinaryComponent>();

    world.entity_mut(entity).insert(OrdinaryComponent{2}, IsResource(ordinary_id));

    EXPECT_FALSE(world.resource_entity<MovableResource>());
    EXPECT_FALSE(world.entity(entity).contains<MovableResource>());
    EXPECT_FALSE(world.entity(entity).contains<OrdinaryComponent>());
    EXPECT_FALSE(world.entity(entity).contains<IsResource>());
}

TEST(ecs, sparse_set_resource_uses_entity_component_storage) {
    World world(WorldId(11));
    world.insert_resource(SparseMovableResource{5});

    auto entity = world.resource_entity<SparseMovableResource>().value();
    EXPECT_TRUE(world.entity(entity).contains<IsResource>());
    EXPECT_EQ(world.entity(entity).get<SparseMovableResource>()->get().value, 5);

    world.entity_mut(entity).get_mut<SparseMovableResource>()->get_mut().value = 8;
    EXPECT_EQ(world.resource<SparseMovableResource>().value, 8);
}

TEST(ecs, resource_take_and_clear_preserve_the_movable_resource_entity) {
    World world(WorldId(12));
    world.insert_resource(MovableResource{3});
    world.emplace_resource<ImmovableResource>(4);
    auto entity = world.resource_entity<MovableResource>().value();

    auto taken = world.take_resource<MovableResource>();
    ASSERT_TRUE(taken);
    EXPECT_EQ(taken->value, 3);
    EXPECT_EQ(world.resource_entity<MovableResource>(), entity);
    EXPECT_FALSE(world.get_resource<MovableResource>());

    world.insert_resource(MovableResource{6});
    world.clear_resources();
    EXPECT_EQ(world.resource_entity<MovableResource>(), entity);
    EXPECT_TRUE(world.entity(entity).contains<IsResource>());
    EXPECT_FALSE(world.get_resource<MovableResource>());
    EXPECT_FALSE(world.get_resource<ImmovableResource>());

    world.insert_resource(MovableResource{9});
    EXPECT_EQ(world.resource_entity<MovableResource>(), entity);
    EXPECT_EQ(world.resource<MovableResource>().value, 9);
}

TEST(ecs, non_movable_resource_uses_explicit_storage) {
    World world(WorldId(3));
    world.emplace_resource<ImmovableResource>(42);

    EXPECT_FALSE(world.resource_entity<ImmovableResource>());
    ASSERT_TRUE(world.get_resource<ImmovableResource>());
    EXPECT_EQ(world.resource<ImmovableResource>().value, 42);

    world.clear_entities();
    ASSERT_TRUE(world.get_resource<ImmovableResource>());
    EXPECT_EQ(world.resource<ImmovableResource>().value, 42);
    EXPECT_TRUE(world.remove_resource<ImmovableResource>());
    EXPECT_FALSE(world.get_resource<ImmovableResource>());
}

TEST(ecs, clear_entities_preserves_movable_resources) {
    World world(WorldId(13));
    world.insert_resource(MovableResource{42});
    auto ordinary = world.spawn(OrdinaryComponent{7}).id();
    auto resource = world.resource_entity<MovableResource>().value();
    int observed  = 0;
    auto system   = make_system_unique([&](Res<MovableResource> value) { observed = value->value; });
    system->initialize(world);

    ASSERT_TRUE(system->run({}, world));
    EXPECT_EQ(observed, 42);

    world.clear_entities();

    EXPECT_FALSE(world.get_entity(ordinary));
    EXPECT_TRUE(world.get_entity(resource));
    EXPECT_EQ(world.resource_entity<MovableResource>(), resource);
    EXPECT_EQ(world.resource<MovableResource>().value, 42);
    observed = 0;
    ASSERT_TRUE(system->run({}, world));
    EXPECT_EQ(observed, 42);
}

TEST(ecs, resource_access_conflicts_with_component_access) {
    World world(WorldId(4));
    // Entity-backed resources and ordinary queries address the same component storage.
    auto system = make_system_unique(conflicting_resource_access);
    EXPECT_THROW(system->initialize(world), std::runtime_error);
}

TEST(ecs, deferred_commands_use_the_matching_resource_storage) {
    World world(WorldId(5));
    auto insert_system = make_system_unique(insert_resources_deferred);
    insert_system->initialize(world);
    ASSERT_TRUE(insert_system->run({}, world));
    EXPECT_EQ(world.resource<MovableResource>().value, 13);
    EXPECT_EQ(world.resource<ImmovableResource>().value, 17);
    EXPECT_TRUE(world.resource_entity<MovableResource>());
    EXPECT_FALSE(world.resource_entity<ImmovableResource>());

    auto remove_system = make_system_unique(remove_resources_deferred);
    remove_system->initialize(world);
    ASSERT_TRUE(remove_system->run({}, world));
    EXPECT_FALSE(world.get_resource<MovableResource>());
    EXPECT_FALSE(world.get_resource<ImmovableResource>());
}

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

struct SparseResource {
    int value = 0;
};
template <>
struct epix::ecs::sparse_component<SparseResource> : std::true_type {};

namespace {
struct TestResource {
    int value = 0;
};

struct OrdinaryComponent {
    int value = 0;
};

struct SecondaryResource {
    explicit SecondaryResource(int value) : value(value) {}
    SecondaryResource(const SecondaryResource&)            = delete;
    SecondaryResource(SecondaryResource&&)                 = default;
    SecondaryResource& operator=(const SecondaryResource&) = delete;
    SecondaryResource& operator=(SecondaryResource&&)      = default;

    int value;
};

void conflicting_resource_access(ResMut<TestResource>, Query<const TestResource&>) {}
void read_resource(Res<TestResource>) {}
void mutate_secondary_resource(ResMut<SecondaryResource> resource) { resource->value += 1; }
void insert_resources_deferred(Commands commands) {
    commands.insert_resource(TestResource{13});
    commands.emplace_resource<SecondaryResource>(17);
}
void remove_resources_deferred(Commands commands) {
    commands.remove_resource<TestResource>();
    commands.remove_resource<SecondaryResource>();
}
}  // namespace

static_assert(std::movable<TestResource>);
static_assert(std::movable<SecondaryResource>);
static_assert(system_param<Res<SecondaryResource>>);
static_assert(system_param<ResMut<SecondaryResource>>);
static_assert(std::constructible_from<ComponentsRegistrator, Components&, ComponentIds&>);
static_assert(!std::constructible_from<ComponentsRegistrator, Components&, ComponentIds&, Archetypes&>);

TEST(ecs, resource_is_an_entity_component) {
    World world(WorldId(1));
    world.insert_resource(TestResource{1});

    auto resource_id     = world.components().get_valid_id<TestResource>().value();
    auto resource_entity = world.resource_entity<TestResource>();
    ASSERT_TRUE(resource_entity);
    EXPECT_EQ(world.resource_entities().get(resource_id), resource_entity);

    auto entity = world.entity(*resource_entity);
    ASSERT_TRUE(entity.contains<TestResource>());
    ASSERT_TRUE(entity.contains<IsResource>());
    EXPECT_EQ(entity.get<IsResource>()->get().resource_component_id(), resource_id);

    world.entity_mut(*resource_entity).get_mut<TestResource>()->get_mut().value = 7;
    EXPECT_EQ(world.resource<TestResource>().value, 7);

    world.entity_mut(*resource_entity).remove<TestResource>();
    EXPECT_FALSE(world.get_resource<TestResource>());
    EXPECT_EQ(world.resource_entity<TestResource>(), resource_entity);

    world.entity_mut(*resource_entity).insert(TestResource{8});
    EXPECT_EQ(world.resource<TestResource>().value, 8);
    EXPECT_TRUE(world.remove_resource<TestResource>());

    world.insert_resource(TestResource{9});
    EXPECT_EQ(world.resource_entity<TestResource>(), resource_entity);
    EXPECT_EQ(world.resource<TestResource>().value, 9);
}

TEST(ecs, system_resource_registration_configures_is_resource) {
    World world(WorldId(6));
    auto system = make_system_unique(read_resource);
    system->initialize(world);

    auto resource_entity = world.spawn(TestResource{21}).id();
    EXPECT_EQ(world.resource_entity<TestResource>(), resource_entity);
    EXPECT_TRUE(world.entity(resource_entity).contains<IsResource>());
    EXPECT_EQ(world.resource<TestResource>().value, 21);
}

TEST(ecs, queued_resource_registration_configures_is_resource) {
    World world(WorldId(9));
    auto resource_id = world.queued_registrator().queue_register_resource<TestResource>();
    world.flush_components();

    auto resource_entity = world.spawn(TestResource{34}).id();
    EXPECT_EQ(world.resource_entities().get(resource_id), resource_entity);
    EXPECT_TRUE(world.entity(resource_entity).contains<IsResource>());
    EXPECT_EQ(world.resource<TestResource>().value, 34);
}

TEST(ecs, all_resource_registrations_require_is_resource) {
    World world(WorldId(14));
    auto registrator = world.registrator();
    auto movable_id  = registrator.register_resource<TestResource>();
    auto second_id   = registrator.register_resource<SecondaryResource>();
    auto marker_id   = world.components().get_valid_id<IsResource>().value();

    EXPECT_TRUE(world.components().get_required_components(movable_id)->get().contains(marker_id));
    EXPECT_TRUE(world.components().get_required_components(second_id)->get().contains(marker_id));

    world.emplace_resource<SecondaryResource>(55);
    EXPECT_TRUE(world.resource_entity<SecondaryResource>());
    EXPECT_EQ(world.resource<SecondaryResource>().value, 55);
}

TEST(ecs, direct_entity_mutation_is_visible_to_resource_change_detection) {
    World world(WorldId(7));
    world.insert_resource(TestResource{1});
    bool modified = false;
    auto system   = make_system_unique([&](Res<TestResource> resource) { modified = resource.is_modified(); });
    system->initialize(world);

    ASSERT_TRUE(system->run({}, world));
    EXPECT_TRUE(modified);
    ASSERT_TRUE(system->run({}, world));
    EXPECT_FALSE(modified);

    world.increment_change_tick();
    auto entity                                                       = world.resource_entity<TestResource>().value();
    world.entity_mut(entity).get_mut<TestResource>()->get_mut().value = 2;
    ASSERT_TRUE(system->run({}, world));
    EXPECT_TRUE(modified);
    EXPECT_EQ(world.resource<TestResource>().value, 2);
}

TEST(ecs, is_resource_enforces_singleton_and_removal_invariants) {
    World world(WorldId(2));
    world.insert_resource(TestResource{1});
    auto resource_id = world.components().get_valid_id<TestResource>().value();
    auto canonical   = world.resource_entity<TestResource>().value();

    auto duplicate = world.spawn(TestResource{2}).id();
    EXPECT_FALSE(world.entity(duplicate).contains<TestResource>());
    EXPECT_FALSE(world.entity(duplicate).contains<IsResource>());
    EXPECT_EQ(world.resource_entity<TestResource>(), canonical);
    EXPECT_EQ(world.resource<TestResource>().value, 1);

    world.entity_mut(canonical).remove<IsResource>();
    EXPECT_FALSE(world.resource_entity<TestResource>());
    EXPECT_FALSE(world.entity(canonical).contains<TestResource>());

    world.insert_resource(TestResource{3});
    auto replacement = world.resource_entity<TestResource>();
    ASSERT_TRUE(replacement);
    EXPECT_NE(*replacement, canonical);
    EXPECT_EQ(world.resource_entities().get(resource_id), replacement);

    world.entity_mut(*replacement).despawn();
    EXPECT_FALSE(world.resource_entity<TestResource>());
    EXPECT_FALSE(world.get_resource<TestResource>());
}

TEST(ecs, component_cannot_become_a_resource_after_entity_use) {
    World world(WorldId(8));
    world.spawn(TestResource{1});
    EXPECT_THROW(world.register_resource<TestResource>(), std::logic_error);
}

TEST(ecs, invalid_is_resource_marker_is_removed_through_entity_access) {
    World world(WorldId(10));
    world.insert_resource(TestResource{1});
    auto entity      = world.resource_entity<TestResource>().value();
    auto ordinary_id = world.registrator().register_component<OrdinaryComponent>();

    world.entity_mut(entity).insert(OrdinaryComponent{2}, IsResource(ordinary_id));

    EXPECT_FALSE(world.resource_entity<TestResource>());
    EXPECT_FALSE(world.entity(entity).contains<TestResource>());
    EXPECT_FALSE(world.entity(entity).contains<OrdinaryComponent>());
    EXPECT_FALSE(world.entity(entity).contains<IsResource>());
}

TEST(ecs, sparse_set_resource_uses_entity_component_storage) {
    World world(WorldId(11));
    world.insert_resource(SparseResource{5});

    auto entity = world.resource_entity<SparseResource>().value();
    EXPECT_TRUE(world.entity(entity).contains<IsResource>());
    EXPECT_EQ(world.entity(entity).get<SparseResource>()->get().value, 5);

    world.entity_mut(entity).get_mut<SparseResource>()->get_mut().value = 8;
    EXPECT_EQ(world.resource<SparseResource>().value, 8);
}

TEST(ecs, resource_take_and_clear_preserve_resource_entities) {
    World world(WorldId(12));
    world.insert_resource(TestResource{3});
    world.emplace_resource<SecondaryResource>(4);
    auto entity           = world.resource_entity<TestResource>().value();
    auto secondary_entity = world.resource_entity<SecondaryResource>().value();

    auto taken = world.take_resource<TestResource>();
    ASSERT_TRUE(taken);
    EXPECT_EQ(taken->value, 3);
    EXPECT_EQ(world.resource_entity<TestResource>(), entity);
    EXPECT_FALSE(world.get_resource<TestResource>());

    world.insert_resource(TestResource{6});
    world.clear_resources();
    EXPECT_EQ(world.resource_entity<TestResource>(), entity);
    EXPECT_TRUE(world.entity(entity).contains<IsResource>());
    EXPECT_FALSE(world.get_resource<TestResource>());
    EXPECT_FALSE(world.get_resource<SecondaryResource>());
    EXPECT_EQ(world.resource_entity<SecondaryResource>(), secondary_entity);

    world.insert_resource(TestResource{9});
    EXPECT_EQ(world.resource_entity<TestResource>(), entity);
    EXPECT_EQ(world.resource<TestResource>().value, 9);
}

TEST(ecs, all_resources_use_entity_component_storage) {
    World world(WorldId(3));
    world.emplace_resource<SecondaryResource>(42);

    auto entity = world.resource_entity<SecondaryResource>();
    ASSERT_TRUE(entity);
    EXPECT_TRUE(world.entity(*entity).contains<SecondaryResource>());
    EXPECT_TRUE(world.entity(*entity).contains<IsResource>());
    ASSERT_TRUE(world.get_resource<SecondaryResource>());
    EXPECT_EQ(world.resource<SecondaryResource>().value, 42);

    world.clear_entities();
    ASSERT_TRUE(world.get_resource<SecondaryResource>());
    EXPECT_EQ(world.resource<SecondaryResource>().value, 42);
    EXPECT_TRUE(world.remove_resource<SecondaryResource>());
    EXPECT_FALSE(world.get_resource<SecondaryResource>());
}

TEST(ecs, secondary_resource_uses_standard_system_param) {
    World world(WorldId(15));
    world.emplace_resource<SecondaryResource>(41);

    auto system = make_system_unique(mutate_secondary_resource);
    system->initialize(world);
    ASSERT_TRUE(system->run({}, world));
    EXPECT_EQ(world.resource<SecondaryResource>().value, 42);
}

TEST(ecs, clear_entities_preserves_resources) {
    World world(WorldId(13));
    world.insert_resource(TestResource{42});
    auto ordinary = world.spawn(OrdinaryComponent{7}).id();
    auto resource = world.resource_entity<TestResource>().value();
    int observed  = 0;
    auto system   = make_system_unique([&](Res<TestResource> value) { observed = value->value; });
    system->initialize(world);

    ASSERT_TRUE(system->run({}, world));
    EXPECT_EQ(observed, 42);

    world.clear_entities();

    EXPECT_FALSE(world.get_entity(ordinary));
    EXPECT_TRUE(world.get_entity(resource));
    EXPECT_EQ(world.resource_entity<TestResource>(), resource);
    EXPECT_EQ(world.resource<TestResource>().value, 42);
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

TEST(ecs, deferred_commands_use_entity_resource_storage) {
    World world(WorldId(5));
    auto insert_system = make_system_unique(insert_resources_deferred);
    insert_system->initialize(world);
    ASSERT_TRUE(insert_system->run({}, world));
    EXPECT_EQ(world.resource<TestResource>().value, 13);
    EXPECT_EQ(world.resource<SecondaryResource>().value, 17);
    EXPECT_TRUE(world.resource_entity<TestResource>());
    EXPECT_TRUE(world.resource_entity<SecondaryResource>());

    auto remove_system = make_system_unique(remove_resources_deferred);
    remove_system->initialize(world);
    ASSERT_TRUE(remove_system->run({}, world));
    EXPECT_FALSE(world.get_resource<TestResource>());
    EXPECT_FALSE(world.get_resource<SecondaryResource>());
}

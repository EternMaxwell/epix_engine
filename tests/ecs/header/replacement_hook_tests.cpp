#include <gtest/gtest.h>

#include <epix/ecs.hpp>
#include <ranges>
#include <type_traits>
#include <vector>

namespace {
namespace ecs = epix::ecs;
enum class Hook { Add, Insert, Replace, Remove, Despawn };
struct Event {
    Hook hook;
    int value;
    bool operator==(const Event&) const = default;
};
struct Events { std::vector<Event> values; };
struct ExtraTable { int value = 0; };
struct ExtraSparse {};

template<bool Sparse> struct Tracked {
    int value = 0;
    static void record(ecs::World& world, ecs::HookContext context, Hook hook) {
        const int value = world.entity(context.entity).get<Tracked>()->get().value;
        world.resource_mut<Events>().values.push_back({hook, value});
    }
    static void on_add(ecs::World& w, ecs::HookContext c) { record(w, c, Hook::Add); }
    static void on_insert(ecs::World& w, ecs::HookContext c) { record(w, c, Hook::Insert); }
    static void on_replace(ecs::World& w, ecs::HookContext c) { record(w, c, Hook::Replace); }
    static void on_remove(ecs::World& w, ecs::HookContext c) { record(w, c, Hook::Remove); }
    static void on_despawn(ecs::World& w, ecs::HookContext c) { record(w, c, Hook::Despawn); }
};
}  // namespace

template<> struct epix::ecs::sparse_component<Tracked<true>> : std::true_type {};
template<> struct epix::ecs::sparse_component<ExtraSparse> : std::true_type {};

namespace {
template<class T> class ReplacementHooks : public testing::Test {
 protected:
    ecs::World world{2};
    ecs::Entity entity;
    void SetUp() override {
        world.init_resource<Events>();
        entity = world.spawn(T{1}).id();
        EXPECT_EQ(events(), (std::vector<Event>{{Hook::Add, 1}, {Hook::Insert, 1}}));
        events().clear();
    }
    std::vector<Event>& events() { return world.resource_mut<Events>().values; }
    void expect_replacement() {
        EXPECT_EQ(events(), (std::vector<Event>{{Hook::Replace, 1}, {Hook::Insert, 2}}));
        EXPECT_EQ(world.entity(entity).template get<T>()->get().value, 2);
        EXPECT_TRUE(std::ranges::empty(world.template removed<T>()));
    }
};
using StorageKinds = testing::Types<Tracked<false>, Tracked<true>>;
TYPED_TEST_SUITE(ReplacementHooks, StorageKinds);

TYPED_TEST(ReplacementHooks, SameArchetypeReplacementIsNotRemoval) {
    this->world.entity_mut(this->entity).insert(TypeParam{2});
    this->expect_replacement();
}

TYPED_TEST(ReplacementHooks, TableTransitionReplacementIsNotRemoval) {
    this->world.entity_mut(this->entity).insert(TypeParam{2}, ExtraTable{});
    this->expect_replacement();
}

TYPED_TEST(ReplacementHooks, SparseArchetypeTransitionReplacementIsNotRemoval) {
    this->world.entity_mut(this->entity).insert(TypeParam{2}, ExtraSparse{});
    this->expect_replacement();
}

TYPED_TEST(ReplacementHooks, DeferredOwnedBatchReplacementIsNotRemoval) {
    auto replace = ecs::make_system_unique([entity = this->entity](ecs::Commands commands) {
        commands.try_insert_batch(std::vector{std::pair{entity, TypeParam{2}}});
    });
    replace->initialize(this->world);
    ASSERT_TRUE(replace->run({}, this->world).has_value());
    replace->apply_deferred(this->world);
    this->expect_replacement();
}

TYPED_TEST(ReplacementHooks, KeepExistingRunsNoHooksForTheRetainedComponent) {
    this->world.entity_mut(this->entity).insert_if_new(TypeParam{2}, ExtraSparse{});
    EXPECT_TRUE(this->events().empty());
    EXPECT_EQ(this->world.entity(this->entity).template get<TypeParam>()->get().value, 1);
}

TYPED_TEST(ReplacementHooks, RemovalRunsReplaceBeforeRemoveWithTheOldValue) {
    this->world.entity_mut(this->entity).template remove<TypeParam>();
    EXPECT_EQ(this->events(), (std::vector<Event>{{Hook::Replace, 1}, {Hook::Remove, 1}}));
    EXPECT_EQ(std::ranges::to<std::vector<ecs::Entity>>(this->world.template removed<TypeParam>()),
              (std::vector{this->entity}));
}

TYPED_TEST(ReplacementHooks, DespawnRunsDespawnReplaceRemoveWithTheOldValue) {
    this->world.entity_mut(this->entity).despawn();
    EXPECT_EQ(this->events(),
              (std::vector<Event>{{Hook::Despawn, 1}, {Hook::Replace, 1}, {Hook::Remove, 1}}));
    EXPECT_FALSE(this->world.get_entity(this->entity));
    EXPECT_EQ(std::ranges::to<std::vector<ecs::Entity>>(this->world.template removed<TypeParam>()),
              (std::vector{this->entity}));
}

TEST(ReplacementHookConsumers, ReparentingDetachesTheOldRelationAndAttachesTheNewOne) {
    ecs::World world{2};
    const auto old_parent = world.spawn().id();
    const auto new_parent = world.spawn().id();
    const auto child = world.entity_mut(old_parent).spawn().id();
    ASSERT_TRUE(world.entity(old_parent).get<ecs::Children>()->get().entities().contains(child));
    world.entity_mut(child).insert(ecs::Parent{new_parent});
    EXPECT_FALSE(world.entity(old_parent).get<ecs::Children>()->get().entities().contains(child));
    EXPECT_TRUE(world.entity(new_parent).get<ecs::Children>()->get().entities().contains(child));
    EXPECT_EQ(world.entity(child).get<ecs::Parent>()->get().entity(), new_parent);
}

TEST(ReplacementHookConsumers, ReplacingTheChildrenCollectionDetachesItsOldRelations) {
    ecs::World world{2};
    const auto parent = world.spawn().id();
    const auto child = world.entity_mut(parent).spawn().id();
    world.entity_mut(parent).insert(ecs::Children{});
    EXPECT_TRUE(world.entity(parent).get<ecs::Children>()->get().entities().empty());
    EXPECT_FALSE(world.entity(child).contains<ecs::Parent>());
    EXPECT_TRUE(world.get_entity(child));
}

TEST(ReplacementHookConsumers, DeferredDespawnRetiresDescendantsAfterTheirOldParentIsGone) {
    ecs::World world{2};
    const auto parent = world.spawn().id();
    const auto child = world.entity_mut(parent).spawn().id();
    const auto grandchild = world.entity_mut(child).spawn().id();
    world.entity_mut(parent).despawn();
    EXPECT_FALSE(world.get_entity(parent));
    EXPECT_FALSE(world.get_entity(child));
    EXPECT_FALSE(world.get_entity(grandchild));
}
}  // namespace

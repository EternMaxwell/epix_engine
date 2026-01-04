module;

#include <gtest/gtest.h>

export module epix.core:tests.hierarchy;

import std;

import :hierarchy;
import :world;

TEST(core, hierarchy) {
    using namespace core;

    auto registry = std::make_shared<TypeRegistry>();
    World world(WorldId(1), std::move(registry));

    // spawn a parent entity
    auto parent_mut = world.spawn();
    Entity parent   = parent_mut.id();

    // spawn a child using the EntityWorldMut::spawn (inserts Parent automatically)
    auto child1_mut = parent_mut.spawn();
    Entity child1   = child1_mut.id();

    // parent should have a Children component containing child1
    auto maybe_parent_ref = world.get_entity(parent);
    ASSERT_TRUE(maybe_parent_ref.has_value()) << "parent not found after spawn";
    auto parent_ref     = *maybe_parent_ref;
    auto maybe_children = parent_ref.get<Children>();
    ASSERT_TRUE(maybe_children.has_value()) << "parent missing Children after spawn via entity ref";
    const auto& children = maybe_children->get();
    EXPECT_NE(children.entities.find(child1), children.entities.end())
        << "child1 not found in parent's Children after entity spawn";

    // now remove Parent from child1 and ensure parent's Children no longer contains it
    world.get_entity_mut(child1).and_then([&](EntityWorldMut&& ew) -> std::optional<bool> {
        ew.remove<Parent>();
        return true;
    });

    // check parent children
    maybe_children = parent_ref.get<Children>();
    EXPECT_EQ(children.entities.find(child1), children.entities.end())
        << "child1 still present in parent's Children after removing Parent from child1";

    // spawn a child via CommandQueue (deferred command style)
    CommandQueue q;
    q.push([parent](World& w) {
        // spawn a child under parent; spawn via entity_mut to ensure hooks run
        if (auto pm = w.get_entity_mut(parent)) {
            pm->spawn();
        }
    });
    q.apply(world);

    // find the newly spawned child (it should be present in parent's Children)
    maybe_parent_ref = world.get_entity(parent);
    ASSERT_TRUE(maybe_parent_ref.has_value()) << "parent not found after command spawn";
    parent_ref     = *maybe_parent_ref;
    maybe_children = parent_ref.get<Children>();
    ASSERT_TRUE(maybe_children.has_value()) << "parent missing Children after spawn via command";

    // there should exist at least one child now (child1 was removed earlier)
    EXPECT_FALSE(maybe_children->get().entities.empty()) << "no children present after command spawn";

    // pick one child from set
    Entity child2 = *maybe_children->get().entities.begin();
    EXPECT_NE(child2, child1) << "child2 is same as child1, expected different entity";

    // despawn the parent and ensure the child is despawned by the Children::on_despawn hook
    world.get_entity_mut(parent).and_then([&](EntityWorldMut&& ew) -> std::optional<bool> {
        ew.despawn();
        return true;
    });

    // child should be despawned
    EXPECT_FALSE(world.get_entity(child2).has_value()) << "child2 still exists after parent despawn";
}
#include <cassert>
#include <iostream>

#include "epix/core/hierarchy.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"
#include "epix/core/world/command_queue.hpp"
#include "epix/core/world/entity_ref.hpp"


using namespace epix::core;
using namespace epix::core::bundle;

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World world(WorldId(1), std::move(registry));

    // spawn a parent entity
    auto parent_mut = world.spawn();
    Entity parent   = parent_mut.id();

    // spawn a child using the EntityWorldMut::spawn (inserts Parent automatically)
    auto child1_mut = parent_mut.spawn();
    Entity child1   = child1_mut.id();

    // parent should have a Children component containing child1
    auto maybe_parent_ref = world.get_entity(parent);
    if (!maybe_parent_ref) {
        std::cerr << "parent not found after spawn\n";
        return 1;
    }
    auto parent_ref     = *maybe_parent_ref;
    auto maybe_children = parent_ref.get<hierarchy::Children>();
    if (!maybe_children.has_value()) {
        std::cerr << "parent missing Children after spawn via entity ref\n";
        return 2;
    }
    const auto& children = maybe_children->get();
    if (children.entities.find(child1) == children.entities.end()) {
        std::cerr << "child1 not found in parent's Children after entity spawn\n";
        return 3;
    }

    // now remove Parent from child1 and ensure parent's Children no longer contains it
    world.get_entity_mut(child1).and_then([&](EntityWorldMut&& ew) -> std::optional<bool> {
        ew.remove<hierarchy::Parent>();
        return true;
    });

    // check parent children
    maybe_children = parent_ref.get<hierarchy::Children>();
    if (maybe_children.has_value() &&
        maybe_children->get().entities.find(child1) != maybe_children->get().entities.end()) {
        std::cerr << "child1 still present in parent's Children after removing Parent from child1\n";
        return 4;
    }

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
    if (!maybe_parent_ref) {
        std::cerr << "parent not found after command spawn\n";
        return 5;
    }
    parent_ref     = *maybe_parent_ref;
    maybe_children = parent_ref.get<hierarchy::Children>();
    if (!maybe_children.has_value()) {
        std::cerr << "parent missing Children after spawn via command\n";
        return 6;
    }

    // there should exist at least one child now (child1 was removed earlier)
    if (maybe_children->get().entities.empty()) {
        std::cerr << "no children present after command spawn\n";
        return 7;
    }

    // pick one child from set
    Entity child2 = *maybe_children->get().entities.begin();
    if (child2 == child1) {
        // unlikely but handle: if it's child1, that's fine — but child1 had parent removed earlier.
    }

    // despawn the parent and ensure the child is despawned by the Children::on_despawn hook
    world.get_entity_mut(parent).and_then([&](EntityWorldMut&& ew) -> std::optional<bool> {
        ew.despawn();
        return true;
    });

    // child should be despawned
    if (auto maybe_child = world.get_entity(child2); maybe_child.has_value()) {
        std::cerr << "child2 still exists after parent despawn\n";
        return 8;
    }

    std::cout << "test_hierarchy passed\n";
    return 0;
}

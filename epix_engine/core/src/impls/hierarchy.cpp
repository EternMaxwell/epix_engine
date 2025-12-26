module;

#include <optional>
#include <ranges>

module epix.core;

import :hierarchy;
import :world;
import :world.entity_ref;

namespace core {
void Parent::on_remove(World& world, HookContext ctx) {
    auto&& this_entity = world.entity_mut(ctx.entity);
    this_entity.get<Parent>().transform([&](const Parent& parent) {
        return world.entity_mut(parent.entity)
            .get_mut<Children>()
            .transform([&](Children& children) {
                children.entities.erase(ctx.entity);
                return true;
            })
            .value_or(false);
    });
}

void Parent::on_insert(World& world, HookContext ctx) {
    auto&& this_entity = world.entity_mut(ctx.entity);
    this_entity.get<Parent>().transform([&](const Parent& parent) {
        return world.entity_mut(parent.entity)
            .get_mut<Children>()
            .or_else([&]() {
                auto entity = world.entity_mut(parent.entity);
                entity.insert_if_new(Children{});
                return entity.get_mut<Children>();
            })
            .transform([&](Children& children) {
                children.entities.insert(ctx.entity);
                return true;
            })
            .value_or(false);
    });
}

void Children::on_remove(World& world, HookContext ctx) {
    auto&& this_entity = world.entity_mut(ctx.entity);
    // copy children set cause removing it inside transform or and_then might invalidate the reference
    std::optional children_to_remove =
        this_entity.get<Children>().transform([&](const Children& children) { return children.entities; });
    children_to_remove.transform([&](std::unordered_set<Entity>& children) {
        for (auto child_entity : children) {
            world.entity_mut(child_entity).remove<Parent>();
        }
        return true;
    });
}

void Children::on_despawn(World& world, HookContext ctx) {
    auto&& this_entity = world.entity_mut(ctx.entity);
    std::optional children_to_remove =
        this_entity.get<Children>().transform([&](const Children& children) { return children.entities; });
    children_to_remove.transform([&](std::unordered_set<Entity>& children) {
        for (auto child_entity : children) {
            world.entity_mut(child_entity).despawn();
        }
        return true;
    });
}

}  // namespace core

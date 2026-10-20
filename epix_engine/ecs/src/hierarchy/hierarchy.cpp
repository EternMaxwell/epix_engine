#include <spdlog/spdlog.h>

#include <epix/ecs/hierarchy/hierarchy.hpp>
#include <epix/ecs/world.hpp>
#include <epix/ecs/world/entity_ref.hpp>

namespace epix::ecs {
void Parent::on_replace(World& world, HookContext ctx) {
    spdlog::trace("[hierarchy] Parent::on_replace for entity {}.", ctx.entity.index);
    auto&& this_entity = world.entity_mut(ctx.entity);
    this_entity.get<Parent>().transform([&](const Parent& parent) {
        auto target = world.get_entity_mut(parent._entity);
        if (!target) return false;  // The parent may already have been despawned.
        return target->get_mut<Children>().transform([&](Children& children) {
            children._entities.erase(ctx.entity);
            return true;
        }).value_or(false);
    });
}

void Parent::on_insert(World& world, HookContext ctx) {
    spdlog::trace("[hierarchy] Parent::on_insert for entity {}.", ctx.entity.index);
    auto&& this_entity = world.entity_mut(ctx.entity);
    this_entity.get<Parent>().transform([&](const Parent& parent) {
        return world.entity_mut(parent._entity)
            .get_mut<Children>()
            .or_else([&]() {
                auto entity = world.entity_mut(parent._entity);
                entity.insert_if_new(Children{});
                return entity.get_mut<Children>();
            })
            .transform([&](Children& children) {
                children._entities.insert(ctx.entity);
                return true;
            })
            .value_or(false);
    });
}

void Children::on_replace(World& world, HookContext ctx) {
    spdlog::trace("[hierarchy] Children::on_replace for entity {}.", ctx.entity.index);
    auto&& this_entity = world.entity_mut(ctx.entity);
    // copy children set cause removing it inside transform or and_then might invalidate the reference
    std::optional children_to_remove =
        this_entity.get<Children>().transform([&](const Children& children) { return children._entities; });
    if (!children_to_remove) return;
    // Bevy's RelationshipTarget::on_replace queues structural changes. Applying
    // them inside this hook would invalidate the enclosing bundle insertion.
    world.command_queue().push([children = std::move(*children_to_remove)](World& deferred) {
        for (auto child_entity : children) {
            if (auto child = deferred.get_entity_mut(child_entity); child && child->contains<Parent>())
                child->remove<Parent>();
        }
    });
}

void Children::on_despawn(World& world, HookContext ctx) {
    spdlog::trace("[hierarchy] Children::on_despawn for entity {} (despawning children).", ctx.entity.index);
    auto&& this_entity = world.entity_mut(ctx.entity);
    std::optional children_to_remove =
        this_entity.get<Children>().transform([&](const Children& children) { return children._entities; });
    if (!children_to_remove) return;
    world.command_queue().push([children = std::move(*children_to_remove)](World& deferred) {
        for (auto child_entity : children) {
            if (auto child = deferred.get_entity_mut(child_entity)) child->despawn();
        }
    });
}

}  // namespace epix::ecs

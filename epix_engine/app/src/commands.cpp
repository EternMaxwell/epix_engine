#include "epix/app/commands.h"

using namespace epix::app;

EPIX_API void DespawnCommand::apply(World& world) { world.despawn(entity); }

EPIX_API void DespawnRecurseCommand::apply(World& world) {
    auto* children = world.entity_try_get<Children>(entity);
    if (children) {
        for (auto& child : (*children).entities) {
            DespawnRecurseCommand{child}.apply(world);
        }
    }
    world.despawn(entity);
}

EPIX_API void RemoveResourceCommand::apply(World& world) {
    world.remove_resource(type);
}

EPIX_API void EntityEraseCommand::apply(World& world) { func(&world, entity); }

EPIX_API Commands::Commands(World& world)
    : world(&world), queue(&world.command_queue()) {}

EPIX_API EntityCommands Commands::entity(Entity entity) {
    return EntityCommands(*world, *queue, entity);
}
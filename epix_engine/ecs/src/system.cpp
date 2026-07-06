#include <epix/ecs/system/commands.hpp>

namespace epix::ecs {
EntityCommands Commands::spawn_empty() noexcept {
    Entity entity = entities->reserve_entity();
    return EntityCommands{entity, *this};
}
EntityCommands Commands::entity(Entity entity) noexcept { return EntityCommands{entity, *this}; }
}  // namespace epix::ecs
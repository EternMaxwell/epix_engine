#include "epix/app.h"

using namespace epix::app;

EPIX_API Commands::Commands(World* world, CommandQueue* command_queue)
    : m_world(world), m_command_queue(command_queue) {}

EPIX_API EntityCommands Commands::entity(const Entity& entity) {
    return EntityCommands(this, m_command_queue, entity);
}
EPIX_API std::optional<EntityCommands> Commands::get_entity(const Entity& entity
) noexcept {
    auto has = m_world->m_registry.valid(entity);
    if (!has) return std::nullopt;
    return EntityCommands(this, m_command_queue, entity);
}
EPIX_API void Commands::add_resource(std::type_index type, UntypedRes res) {
    m_world->add_resource(type, res);
}

EPIX_API void EntityCommands::despawn() { m_command_queue->despawn(m_entity); }
EPIX_API void EntityCommands::despawn_recurse() {
    m_command_queue->despawn_recurse(m_entity);
}
EPIX_API Entity EntityCommands::id() const { return m_entity; }
EPIX_API EntityCommands::EntityCommands(
    Commands* commands, CommandQueue* command_queue, Entity entity
)
    : m_world(commands->m_world),
      m_command(commands),
      m_command_queue(command_queue),
      m_entity(entity) {}

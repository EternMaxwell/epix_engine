#include "epix/app/world.h"

using namespace epix::app;

EPIX_API World::World(const WorldLabel& label) : m_label(label) {}
EPIX_API CommandQueue& World::command_queue() { return m_command_queue; }
EPIX_API entt::registry& World::registry() { return m_data.registry; }
EPIX_API void World::remove_resource(const std::type_index& type) {
    auto resources = m_data.resources.write();
    resources->erase(type);
}
EPIX_API bool World::entity_valid(Entity entity) {
    return m_data.registry.valid(entity);
}

EPIX_API void World::despawn(Entity entity) {
    if (!m_data.registry.valid(entity)) return;
    m_data.registry.destroy(entity);
}
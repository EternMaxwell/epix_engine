#include "epix/app/world.h"

using namespace epix::app;

EPIX_API World::World(const WorldLabel& label) : m_label(label) {}
EPIX_API CommandQueue& World::command_queue() { return m_command_queue; }
EPIX_API entt::registry& World::registry() { return m_data.registry; }
EPIX_API bool World::entity_valid(Entity entity) { return m_data.registry.valid(entity); }

EPIX_API void World::add_resource(meta::type_index id, const std::shared_ptr<void>& res) {
    auto resources = m_data.resources.write();
    if (!resources->contains(id)) {
        resources->emplace(id, res);
    }
};

EPIX_API void World::despawn(Entity entity) {
    if (!m_data.registry.valid(entity)) return;
    m_data.registry.destroy(entity);
}

EPIX_API void World::clear_entities() { m_data.registry.clear(); }
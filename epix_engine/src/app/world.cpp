#include "epix/app/world.h"

using namespace epix::app;

EPIX_API World::World()
    : m_data(std::make_unique<WorldData>()),
      m_command_queue(std::make_unique<CommandQueue>()) {}
EPIX_API CommandQueue& World::command_queue() { return *m_command_queue; }
EPIX_API entt::registry& World::registry() { return m_data->registry; }
EPIX_API void World::add_resource(const UntypedRes& res) {
    std::unique_lock lock(m_data->resources_mutex);
    if (m_data->resources.contains(res.type)) {
        return;
    }
    m_data->resources.emplace(res.type, res);
}
EPIX_API void World::remove_resource(const std::type_index& type) {
    std::unique_lock lock(m_data->resources_mutex);
    m_data->resources.erase(type);
}
EPIX_API bool World::entity_valid(Entity entity) {
    return m_data->registry.valid(entity);
}

EPIX_API void World::despawn(Entity entity) {
    if (!m_data->registry.valid(entity)) return;
    m_data->registry.destroy(entity);
}

EPIX_API UntypedRes World::untyped_resource(const std::type_index& type) const {
    std::shared_lock lock(m_data->resources_mutex);
    auto it = m_data->resources.find(type);
    if (it != m_data->resources.end()) {
        return it->second;
    }
    throw std::runtime_error("Resource not found: " + std::string(type.name()));
}
EPIX_API std::optional<UntypedRes> World::get_untyped_resource(
    const std::type_index& type
) const {
    std::shared_lock lock(m_data->resources_mutex);
    auto it = m_data->resources.find(type);
    if (it != m_data->resources.end()) {
        return it->second;
    }
    return std::nullopt;
}
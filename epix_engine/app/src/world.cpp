#include "epix/app.h"

using namespace epix::app;

EPIX_API World::World() {}
EPIX_API World::~World() {
    m_command_queue.flush(*this);
    m_registry.clear();
    m_resources.clear();
}

EPIX_API std::shared_ptr<void> World::get_resource(const std::type_index& type
) const {
    std::shared_lock lock(m_resources_mutex);
    auto it = m_resources.find(type);
    if (it != m_resources.end()) {
        return it->second;
    }
    return nullptr;
}
EPIX_API void World::add_resource(
    std::type_index type, std::shared_ptr<void> res
) {
    std::unique_lock lock(m_resources_mutex);
    if (!m_resources.contains(type)) {
        m_resources.emplace(type, res);
    }
}
EPIX_API void World::remove_resource(const std::type_index& type) {
    std::unique_lock lock(m_resources_mutex);
    auto it = m_resources.find(type);
    if (it != m_resources.end()) {
        m_resources.erase(it);
    }
}

EPIX_API void CommandQueue::flush(World& world) {
    std::unique_lock lock(m_mutex);
    for (auto&& each : m_commands) {
        uint32_t index = each >> 24;
        uint32_t id    = each & 0x00FFFFFF;
        if (index == 0) {
            world.m_registry.destroy(m_despawn_list[id]);
        } else if (index == 1) {
            auto& children =
                world.m_registry.get_or_emplace<Children>(m_despawn_list[id]);
            for (auto&& child : children.children) {
                world.m_registry.destroy(child);
            }
            world.m_registry.destroy(m_despawn_list[id]);
        } else if (index == 2) {
            world.remove_resource(m_remove_resources_list[id]);
        } else if (index == 3) {
            auto&& [func, entity] = m_entity_erase_list[id];
            func(&world, entity);
        }
    }
    m_commands.clear();
    m_despawn_list.clear();
    m_recurse_despawn_list.clear();
    m_remove_resources_list.clear();
    m_entity_erase_list.clear();
}

EPIX_API void CommandQueue::despawn(const Entity& entity) {
    m_commands.emplace_back(0 << 24 | m_despawn_list.size());
    m_despawn_list.emplace_back(entity);
}
EPIX_API void CommandQueue::despawn_recurse(const Entity& entity) {
    m_commands.emplace_back(1 << 24 | m_recurse_despawn_list.size());
    m_recurse_despawn_list.emplace_back(entity);
}
EPIX_API void CommandQueue::remove_resource(const std::type_index& type) {
    m_commands.emplace_back(2 << 24 | m_remove_resources_list.size());
    m_remove_resources_list.emplace_back(type);
}
EPIX_API void CommandQueue::entity_erase(
    void (*func)(World*, Entity), const Entity& entity
) {
    m_commands.emplace_back(3 << 24 | m_entity_erase_list.size());
    m_entity_erase_list.emplace_back(func, entity);
}
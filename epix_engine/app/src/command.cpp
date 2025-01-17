#include "epix/app.h"

using namespace epix::app;

EPIX_API EntityCommand::EntityCommand(
    entt::registry* registry,
    Entity entity,
    entt::dense_set<Entity>* despawns,
    entt::dense_set<Entity>* recursive_despawns
)
    : m_registry(registry),
      m_entity(entity),
      m_despawns(despawns),
      m_recursive_despawns(recursive_despawns) {}

EPIX_API void EntityCommand::despawn() { m_despawns->emplace(m_entity); }

EPIX_API void EntityCommand::despawn_recurse() {
    m_recursive_despawns->emplace(m_entity);
}

EPIX_API Entity EntityCommand::id() { return m_entity; }

EPIX_API EntityCommand::operator Entity() { return m_entity; }

EPIX_API Command::Command(World* world, World* src)
    : m_world(world),
      m_src(src),
      m_despawns(std::make_shared<entt::dense_set<Entity>>()),
      m_recursive_despawns(std::make_shared<entt::dense_set<Entity>>()),
      m_resource_removers(
          std::make_shared<std::vector<std::function<void(World*)>>>()
      ) {}

EPIX_API EntityCommand Command::entity(Entity entity) {
    return EntityCommand(
        &m_world->m_registry, entity, m_despawns.get(),
        m_recursive_despawns.get()
    );
}

EPIX_API void Command::end() {
    auto m_registry = &m_world->m_registry;
    for (auto entity : *m_recursive_despawns) {
        auto& children = m_registry->get_or_emplace<Children>(entity);
        for (auto child : children.children) {
            m_registry->destroy(child);
        }
        m_registry->destroy(entity);
    }
    m_recursive_despawns->clear();
    for (auto entity : *m_despawns) {
        m_registry->destroy(entity);
    }
    m_despawns->clear();
    for (auto remover : *m_resource_removers) {
        remover(m_world);
    }
    m_resource_removers->clear();
}
#include "epix/app.h"

using namespace epix::app;

EPIX_API EntityCommand Command::entity(Entity entity) {
    return EntityCommand(dst_cmd->m_world, dst_cmd, entity);
}

EPIX_API Command::Command(WorldCommand* src, WorldCommand* dst)
    : src_cmd(src), dst_cmd(dst) {}

EPIX_API WorldEntityCommand WorldCommand::entity(Entity entity) {
    return WorldEntityCommand(m_world, this, entity);
}

EPIX_API WorldCommand::WorldCommand(World* world) : m_world(world) {}
EPIX_API void WorldCommand::flush() {
    while (auto&& opt = m_despawn.try_pop()) {
        auto entity = *opt;
        if (m_world->m_registry.valid(entity)) {
            m_world->m_registry.destroy(entity);
        }
    }
    while (auto&& opt = m_recurse_despawn.try_pop()) {
        auto entity = *opt;
        if (m_world->m_registry.valid(entity)) {
            for (auto&& child :
                 m_world->m_registry.get_or_emplace<Children>(entity)
                     .children) {
                m_recurse_despawn.emplace(child);
            }
            m_world->m_registry.destroy(entity);
        }
    }
    flush_relax();
}
EPIX_API void WorldCommand::flush_relax() {
    while (auto&& opt = m_remove_resources.try_pop()) {
        auto res_type = *opt;
        m_world->m_resources.erase(res_type);
    }
    while (auto&& opt = m_entity_erase.try_pop()) {
        auto&& [func, entity] = *opt;
        if (m_world->m_registry.valid(entity)) {
            func(m_world, entity);
        }
    }
    // while (auto&& opt = m_add_resources.try_pop()) {
    //     auto&& [type, res, mutex] = *opt;
    //     if (!m_world->m_resources.contains(type)) {
    //         m_world->m_resources.emplace(type, UntypedRes{res, mutex});
    //     }
    // }
}

EPIX_API WorldEntityCommand::WorldEntityCommand(
    World* world, WorldCommand* command, Entity entity
)
    : m_world(world), m_command(command), m_entity(entity) {}
EPIX_API void WorldEntityCommand::despawn() {
    m_command->m_despawn.emplace(m_entity);
}
EPIX_API void WorldEntityCommand::despawn_recurse() {
    m_command->m_recurse_despawn.emplace(m_entity);
}
EPIX_API Entity WorldEntityCommand::id() const { return m_entity; }
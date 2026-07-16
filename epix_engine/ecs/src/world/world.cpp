#include <epix/ecs/world.hpp>

namespace epix::ecs {

void World::flush_entities() {
    auto& empty_archetype = _archetypes.get_empty_mut();
    auto& empty_table     = _storage.tables.get_mut(empty_archetype.table_id()).value().get();
    _entities.flush([&](Entity entity, EntityLocation& location) {
        location = empty_archetype.allocate(entity, empty_table.allocate(entity));
    });
}

void World::flush_commands() { _command_queue.apply(*this); }

void World::flush() {
    flush_entities();
    flush_commands();
}

namespace internal {

WorldId world_id(const World& world) noexcept { return world.id(); }
const Components& world_components(const World& world) noexcept { return world.components(); }
Components& world_components_mut(World& world) noexcept { return world.components_mut(); }
ComponentsRegistrator world_registrator(World& world) noexcept { return world.registrator(); }
ComponentsQueuedRegistrator world_queued_registrator(World& world) noexcept { return world.queued_registrator(); }
const Entities& world_entities(const World& world) noexcept { return world.entities(); }
Entities& world_entities_mut(World& world) noexcept { return world.entities_mut(); }
const Storage& world_storage(const World& world) noexcept { return world.storage(); }
Storage& world_storage_mut(World& world) noexcept { return world.storage_mut(); }
const Archetypes& world_archetypes(const World& world) noexcept { return world.archetypes(); }
Archetypes& world_archetypes_mut(World& world) noexcept { return world.archetypes_mut(); }
const Bundles& world_bundles(const World& world) noexcept { return world.bundles(); }
Bundles& world_bundles_mut(World& world) noexcept { return world.bundles_mut(); }
CommandQueue& world_command_queue(World& world) noexcept { return world.command_queue(); }
Tick world_change_tick(const World& world) noexcept { return world.change_tick(); }
Tick world_increment_change_tick(World& world) noexcept { return world.increment_change_tick(); }
Tick world_last_change_tick(const World& world) noexcept { return world.last_change_tick(); }
void world_flush_entities(World& world) { world.flush_entities(); }
void world_flush_commands(World& world) { world.flush_commands(); }
void world_flush(World& world) { world.flush(); }

WorldId world_id(const DeferredWorld& world) noexcept { return world.id(); }
const Entities& world_entities(const DeferredWorld& world) noexcept { return world.entities(); }
const Storage& world_storage(const DeferredWorld& world) noexcept { return world.storage(); }
const Components& world_components(const DeferredWorld& world) noexcept { return world.components(); }
ComponentsQueuedRegistrator world_queued_registrator(const DeferredWorld& world) noexcept {
    return world.queued_registrator();
}
const Archetypes& world_archetypes(const DeferredWorld& world) noexcept { return world.archetypes(); }
const Bundles& world_bundles(const DeferredWorld& world) noexcept { return world.bundles(); }
CommandQueue& world_command_queue(DeferredWorld& world) noexcept { return world.command_queue(); }
Tick world_change_tick(const DeferredWorld& world) noexcept { return world.change_tick(); }
Tick world_last_change_tick(const DeferredWorld& world) noexcept { return world.last_change_tick(); }

}  // namespace internal
}  // namespace epix::ecs

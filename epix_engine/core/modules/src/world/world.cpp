module epix.core;

import :world;

using namespace core;

const TypeRegistry& core::world_type_registry(const World& world) { return world.type_registry(); }
std::shared_ptr<TypeRegistry> core::world_type_registry_ptr(const World& world) { return world.type_registry_ptr(); }
const Components& core::world_components(const World& world) { return world.components(); }
Components& core::world_components_mut(World& world) { return world.components_mut(); }
const Entities& core::world_entities(const World& world) { return world.entities(); }
Entities& core::world_entities_mut(World& world) { return world.entities_mut(); }
const Storage& core::world_storage(const World& world) { return world.storage(); }
Storage& core::world_storage_mut(World& world) { return world.storage_mut(); }
const Archetypes& core::world_archetypes(const World& world) { return world.archetypes(); }
Archetypes& core::world_archetypes_mut(World& world) { return world.archetypes_mut(); }
const Bundles& core::world_bundles(const World& world) { return world.bundles(); }
Bundles& core::world_bundles_mut(World& world) { return world.bundles_mut(); }
Tick core::world_change_tick(const World& world) { return world.change_tick(); }
Tick core::world_increment_change_tick(World& world) { return world.increment_change_tick(); }
Tick core::world_last_change_tick(const World& world) { return world.last_change_tick(); }
void core::world_flush_entities(World& world) { world.flush_entities(); }
void core::world_flush_commands(World& world) { world.flush_commands(); }
void core::world_flush(World& world) { world.flush(); }
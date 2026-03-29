module epix.core;

import :world;

namespace epix::core {

WorldId world_id(const World& world) { return world.id(); }
const TypeRegistry& world_type_registry(const World& world) { return world.type_registry(); }
std::shared_ptr<TypeRegistry> world_type_registry_ptr(const World& world) { return world.type_registry_ptr(); }
const Components& world_components(const World& world) { return world.components(); }
Components& world_components_mut(World& world) { return world.components_mut(); }
const Entities& world_entities(const World& world) { return world.entities(); }
Entities& world_entities_mut(World& world) { return world.entities_mut(); }
const Storage& world_storage(const World& world) { return world.storage(); }
Storage& world_storage_mut(World& world) { return world.storage_mut(); }
const Archetypes& world_archetypes(const World& world) { return world.archetypes(); }
Archetypes& world_archetypes_mut(World& world) { return world.archetypes_mut(); }
const Bundles& world_bundles(const World& world) { return world.bundles(); }
Bundles& world_bundles_mut(World& world) { return world.bundles_mut(); }
Tick world_change_tick(const World& world) { return world.change_tick(); }
Tick world_increment_change_tick(World& world) { return world.increment_change_tick(); }
Tick world_last_change_tick(const World& world) { return world.last_change_tick(); }
void world_flush_entities(World& world) { world.flush_entities(); }
void world_flush_commands(World& world) { world.flush_commands(); }
void world_flush(World& world) { world.flush(); }

}  // namespace epix::core

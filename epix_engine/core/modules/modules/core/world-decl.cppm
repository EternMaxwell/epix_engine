module;

export module epix.core:world.decl;

import std;

import :utils;
import :tick;
import :type_registry;
import :entities;

namespace core {
export struct World;
export struct DeferredWorld;

export struct WorldId : ::core::int_base<std::uint64_t> {
    using int_base::int_base;
};

WorldId world_id(const World& world);
const TypeRegistry& world_type_registry(const World& world);
std::shared_ptr<TypeRegistry> world_type_registry_ptr(const World& world);
const Entities& world_entities(const World& world);
Entities& world_entities_mut(World& world);
Tick world_change_tick(const World& world);
Tick world_increment_change_tick(World& world);
Tick world_last_change_tick(const World& world);
void world_flush_entities(World& world);
void world_flush_commands(World& world);
void world_flush(World& world);
}  // namespace core
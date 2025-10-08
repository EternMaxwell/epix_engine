#pragma once

#include <atomic>
#include <memory>

#include "bundle.hpp"
#include "component.hpp"
#include "entities.hpp"
#include "fwd.hpp"
#include "storage.hpp"
#include "type_system/type_registry.hpp"

namespace epix::core {
EPIX_MAKE_U64_WRAPPER(WorldId)
/**
 * @brief A ecs world that holds all entities and components, and also resources.
 */
struct WorldCell {
   public:
    WorldCell(WorldId id,
              std::shared_ptr<type_system::TypeRegistry> type_registry = std::make_shared<type_system::TypeRegistry>())
        : _id(id),
          _type_registry(std::move(type_registry)),
          _storage(_type_registry),
          _change_tick(std::make_unique<std::atomic<uint32_t>>(1)),
          _last_change_tick(0) {}

    WorldId id() const { return _id; }
    const type_system::TypeRegistry& type_registry() const { return *_type_registry; }
    const Components& components() const { return _components; }
    Components& components_mut() { return _components; }
    const Entities& entities() const { return _entities; }
    Entities& entities_mut() { return _entities; }
    const Storage& storage() const { return _storage; }
    Storage& storage_mut() { return _storage; }
    const Archetypes& archetypes() const { return _archetypes; }
    Archetypes& archetypes_mut() { return _archetypes; }
    const Bundles& bundles() const { return _bundles; }
    Bundles& bundles_mut() { return _bundles; }
    Tick change_tick() const { return _change_tick->load(std::memory_order_relaxed); }
    Tick last_change_tick() const { return _last_change_tick; }

    void flush_entities() {
        auto& empty_archetype = _archetypes.get_empty_mut();
        auto& empty_table     = _storage.tables.get_mut(empty_archetype.table_id()).value().get();
        _entities.flush([&](Entity entity, EntityLocation& location) {
            location = empty_archetype.allocate(entity, empty_table.allocate(entity));
        });
    }
    void flush() { flush_entities(); }

   protected:
    WorldId _id;
    std::shared_ptr<type_system::TypeRegistry> _type_registry;
    Components _components;
    Entities _entities;
    Storage _storage;
    Archetypes _archetypes;
    Bundles _bundles;
    std::unique_ptr<std::atomic<uint32_t>> _change_tick;
    Tick _last_change_tick;
};
}  // namespace epix::core
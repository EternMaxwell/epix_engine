module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <cassert>

export module epix.core:bundle.spawner;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :bundle.info;
import :world.decl;

namespace epix::core {

// Bundle spawner for insert entities newly spawned (that is, not actually added to any archetype yet, otherwise will
// adding a 'move from empty archetype to target archetype' operation)
struct BundleSpawner {
   public:
    static BundleSpawner create_with_id(World& world, BundleId bundle_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        auto& components   = world_components_mut(world);
        auto& storage      = world_storage_mut(world);
        auto& archetypes   = world_archetypes_mut(world);
        auto&& bundle_info = bundles.unsafe_get(bundle_id);
        ArchetypeId new_archetype_id =
            bundle_info.insert_bundle_into_archetype(archetypes, storage, components, ArchetypeId(0));
        Archetype& archetype = archetypes.unsafe_get_mut(new_archetype_id);
        BundleSpawner spawner;
        spawner.world_       = &world;
        spawner.bundle_info_ = &bundle_info;
        spawner.archetype_   = &archetype;
        spawner.table_       = &storage.tables.unsafe_get_mut(archetype.table_id());
        spawner.change_tick_ = tick;
        return spawner;
    }
    template <is_bundle T>
    static BundleSpawner create(World& world, Tick tick) {
        auto& bundles = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, bundle_id, tick);
    }

    void reserve_storage(std::size_t additional) {
        if (additional == 0) return;
        auto& table     = *table_;
        auto& archetype = *archetype_;
        table.reserve(table.size() + additional);
        archetype.reserve(archetype.size() + additional);
    }
    template <typename T>
    EntityLocation spawn_non_exist(Entity entity, T&& bundle)
        requires is_bundle<std::decay_t<T>>
    {
        auto& bundle_info = *bundle_info_;
        auto& archetype   = *archetype_;
        auto& table       = *table_;
        TableRow row      = table.allocate(entity);
        auto location     = archetype.allocate(entity, row);
        world_entities_mut(*world_).set(entity.index, location);
        auto spawn_bundle_status = std::views::take(std::views::repeat(ComponentStatus::Added),
                                                    std::ranges::size(bundle_info.explicit_components()));
        bundle_info.write_components(table, world_storage_mut(*world_).sparse_sets, world_type_registry(*world_),
                                     world_components(*world_), spawn_bundle_status,
                                     bundle_info.required_component_constructors(), entity, row, change_tick_, bundle,
                                     InsertMode::Replace);
        // trigger on_add for newly added components in the bundle
        world_trigger_on_add(*world_, archetype, entity, archetype.components());
        // trigger on_insert for newly added components in the bundle
        world_trigger_on_insert(*world_, archetype, entity, archetype.components());

        location = world_entities(*world_).unsafe_get(entity);  // in case it may be changed by on_add or on_insert
        return location;
    }

   private:
    World* world_ = nullptr;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Table* table_;
    Tick change_tick_;
};
}  // namespace epix::core
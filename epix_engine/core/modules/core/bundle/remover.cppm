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

export module epix.core:bundle.remover;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :bundle.info;
import :world.decl;

namespace epix::core {
struct BundleRemover {
   public:
    static BundleRemover create_with_id(World& world, ArchetypeId archetype_id, BundleId bundle_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        auto& components   = world_components_mut(world);
        auto& storage      = world_storage_mut(world);
        auto& archetypes   = world_archetypes_mut(world);
        auto&& bundle_info = bundles.get(bundle_id).value().get();
        auto opt_archetype_id =
            bundle_info.remove_bundle_from_archetype(archetypes, storage, components, archetype_id, true);
        if (!opt_archetype_id) {
            throw std::logic_error("cannot remove bundle from archetype that does not contain all its components");
        }
        ArchetypeId new_archetype_id = opt_archetype_id.value();
        Archetype& archetype         = archetypes.get_mut(archetype_id).value().get();
        BundleRemover remover;
        remover.world_         = &world;
        remover.bundle_info_   = &bundle_info;
        remover.archetype_     = &archetype;
        remover.new_archetype_ = &archetypes.get_mut(new_archetype_id).value().get();
        remover.table_         = &storage.tables.get_mut(archetype.table_id()).value().get();
        remover.change_tick_   = tick;
        return remover;
    }
    template <is_bundle T>
    static BundleRemover create(World& world, ArchetypeId archetype_id, Tick tick) {
        auto& bundles = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, archetype_id, bundle_id, tick);
    }
    static BundleRemover create_with_type_id(World& world, ArchetypeId archetype_id, TypeId type_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        BundleId bundle_id = bundles.init_component_info(world_storage_mut(world), world_components(world), type_id);
        return create_with_id(world, archetype_id, bundle_id, tick);
    }

    EntityLocation remove(Entity entity, EntityLocation location) {
        assert(location.archetype_id == archetype_->id());
        // Not templated on bundle type, since we don't need to write components
        auto& bundle_info    = *bundle_info_;
        auto& dest_archetype = *new_archetype_;
        auto& src_archetype  = *archetype_;

        // trigger on_remove for components in the bundle
        world_trigger_on_remove(*world_, src_archetype, entity, bundle_info.explicit_components());

        location = world_entities(*world_).get(entity).value();  // in case it may be changed by on_remove

        auto result = src_archetype.swap_remove(location.archetype_idx);
        if (result.swapped_entity) {
            // swapped entity should update its location
            auto swapped_entity            = result.swapped_entity.value();
            auto swapped_location          = world_entities(*world_).get(swapped_entity).value();
            swapped_location.archetype_idx = location.archetype_idx;
            world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
        }
        bool same_table     = (src_archetype.table_id() == dest_archetype.table_id());
        bool same_archetype = (src_archetype.id() == dest_archetype.id());
        if (!same_table) {
            auto& new_table  = world_storage_mut(*world_).tables.get_mut(dest_archetype.table_id()).value().get();
            auto move_result = table_->move_to(result.table_row, new_table);
            if (move_result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity        = move_result.swapped_entity.value();
                auto swapped_location      = world_entities(*world_).get(swapped_entity).value();
                swapped_location.table_idx = result.table_row;
                world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                auto& swapped_archetype =
                    world_archetypes_mut(*world_).get_mut(swapped_location.archetype_id).value().get();
                swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
            }
            location = dest_archetype.allocate(entity, move_result.new_index);
        } else {
            location = dest_archetype.allocate(entity, result.table_row);
        }
        for (auto&& type_id : bundle_info.explicit_components()) {
            world_components(*world_).get(type_id).and_then([&](const ComponentInfo& info) -> std::optional<bool> {
                // Not registered component will be ignored
                auto storage_type = info.storage_type();
                if (storage_type == StorageType::SparseSet) {
                    auto& sparse_set = world_storage_mut(*world_).sparse_sets.get_mut(type_id).value().get();
                    if (sparse_set.contains(entity)) sparse_set.remove(entity);
                }
                return true;
            });
        }
        world_entities_mut(*world_).set(entity.index, location);
        return location;
    }

   private:
    World* world_ = nullptr;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Archetype* new_archetype_;
    Table* table_;
    Tick change_tick_;
};
}  // namespace epix::core
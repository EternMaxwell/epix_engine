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

export module epix.core:bundle.inserter;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import :bundle.info;
import :world.decl;

namespace epix::core {
struct BundleInserter {
   public:
    static BundleInserter create_with_id(World& world, ArchetypeId archetype_id, BundleId bundle_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        auto& components   = world_components_mut(world);
        auto& storage      = world_storage_mut(world);
        auto& archetypes   = world_archetypes_mut(world);
        auto&& bundle_info = bundles.unsafe_get(bundle_id);
        ArchetypeId new_archetype_id =
            bundle_info.insert_bundle_into_archetype(archetypes, storage, components, archetype_id);
        Archetype& archetype = archetypes.unsafe_get_mut(archetype_id);
        BundleInserter inserter;
        inserter.world_                  = &world;
        inserter.archetype_after_insert_ = &archetype.edges().unsafe_archetype_after_bundle_insert_detail(bundle_id);
        inserter.bundle_info_            = &bundle_info;
        inserter.archetype_              = &archetype;
        inserter.table_                  = &storage.tables.unsafe_get_mut(archetype.table_id());
        inserter.change_tick_            = tick;
        return inserter;
    }
    template <is_bundle T>
    static BundleInserter create(World& world, ArchetypeId archetype_id, Tick tick) {
        auto& components = world_components(world);
        auto& bundles    = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, archetype_id, bundle_id, tick);
    }

    EntityLocation insert(Entity entity,
                          EntityLocation location,
                          is_bundle auto&& bundle,
                          InsertMode insert_mode) const {
        assert(location.archetype_id == archetype_->id());
        auto& bundle_info         = *bundle_info_;
        auto& dest_archetype      = world_archetypes_mut(*world_).unsafe_get_mut(archetype_after_insert_->archetype_id);
        const bool same_archetype = (archetype_->id() == dest_archetype.id());
        const bool same_table     = (archetype_->table_id() == dest_archetype.table_id());
        const bool should_replace = insert_mode == InsertMode::Replace;

        // trigger on_replace if replacing existing components in the bundle
        if (should_replace) {
            world_trigger_on_replace(*world_, *archetype_, entity, archetype_after_insert_->existing());
            world_trigger_on_remove(*world_, *archetype_, entity, archetype_after_insert_->existing());
        }
        location = world_entities(*world_).unsafe_get(entity);  // in case it may be changed by on_replace

        location = [&] {
            if (same_archetype) {
                // same archetype, just write components in place
                bundle_info.write_components(*table_, world_storage_mut(*world_).sparse_sets,
                                             world_type_registry(*world_), world_components(*world_),
                                             archetype_after_insert_->iter_status(),
                                             std::views::all(archetype_after_insert_->required_components), entity,
                                             location.table_idx, change_tick_, bundle, insert_mode);
                // location not changed
                return location;
            } else if (same_table) {
                // table not changed, but archetype changed due to sparse components
                auto result = archetype_->swap_remove(location.archetype_idx);
                if (result.swapped_entity) {
                    // swapped entity should update its location
                    auto swapped_entity            = result.swapped_entity.value();
                    auto swapped_location          = world_entities(*world_).unsafe_get(swapped_entity);
                    swapped_location.archetype_idx = location.archetype_idx;
                    world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                }
                auto new_location = dest_archetype.allocate(entity, result.table_row);
                world_entities_mut(*world_).set(entity.index, new_location);
                bundle_info.write_components(*table_, world_storage_mut(*world_).sparse_sets,
                                             world_type_registry(*world_), world_components(*world_),
                                             archetype_after_insert_->iter_status(),
                                             std::views::all(archetype_after_insert_->required_components), entity,
                                             result.table_row, change_tick_, bundle, insert_mode);
                return new_location;
            } else {
                auto& new_table = world_storage_mut(*world_).tables.unsafe_get_mut(dest_archetype.table_id());
                auto& table     = *table_;
                auto result     = archetype_->swap_remove(location.archetype_idx);
                if (result.swapped_entity) {
                    // swapped entity should update its location
                    auto swapped_entity            = result.swapped_entity.value();
                    auto swapped_location          = world_entities(*world_).unsafe_get(swapped_entity);
                    swapped_location.archetype_idx = location.archetype_idx;
                    world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                }
                auto move_result  = table.move_to(result.table_row, new_table);
                auto new_location = dest_archetype.allocate(entity, move_result.new_index);
                world_entities_mut(*world_).set(entity.index, new_location);
                if (move_result.swapped_entity) {
                    // swapped entity should update its location
                    auto swapped_entity        = move_result.swapped_entity.value();
                    auto swapped_location      = world_entities(*world_).unsafe_get(swapped_entity);
                    swapped_location.table_idx = result.table_row;
                    world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                    auto& swapped_archetype =
                        world_archetypes_mut(*world_).unsafe_get_mut(swapped_location.archetype_id);
                    swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
                }
                bundle_info.write_components(new_table, world_storage_mut(*world_).sparse_sets,
                                             world_type_registry(*world_), world_components(*world_),
                                             archetype_after_insert_->iter_status(),
                                             std::views::all(archetype_after_insert_->required_components), entity,
                                             move_result.new_index, change_tick_, bundle, insert_mode);
                return new_location;
            }
        }();
        // trigger on_add for newly added components in the bundle
        world_trigger_on_add(*world_, dest_archetype, entity, archetype_after_insert_->added());
        // trigger on_insert for newly added components in the bundle and existing components if replaced
        if (should_replace) {
            world_trigger_on_insert(*world_, dest_archetype, entity, archetype_after_insert_->inserted());
        } else {
            world_trigger_on_insert(*world_, dest_archetype, entity, archetype_after_insert_->added());
        }
        location = world_entities(*world_).unsafe_get(entity);  // in case it may be changed by on_add or on_insert

        return location;
    }

   private:
    World* world_ = nullptr;
    const ArchetypeAfterBundleInsert* archetype_after_insert_;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Table* table_;
    Tick change_tick_;
};
}  // namespace epix::core
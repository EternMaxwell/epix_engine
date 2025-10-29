#include "epix/core/world/entity_ref.hpp"

namespace epix::core {

void EntityWorldMut::remove_bundle(BundleId bundle_id) {
    assert_not_despawned();
    auto remover =
        bundle::BundleRemover::create_with_id(*world_, location_.archetype_id, bundle_id, world_->change_tick());
    location_ = remover.remove(entity_, location_);
    world_->flush();
    update_location();
}

bool EntityWorldMut::remove_by_id(TypeId type_id) {
    if (!archetype().contains(type_id)) return false;
    auto bundle_id = world_->bundles_mut().init_component_info(world_->storage_mut(), world_->components(), type_id);
    remove_bundle(bundle_id);
    return true;
}

void EntityWorldMut::clear() {
    assert_not_despawned();
    auto& archetype = world_->archetypes_mut().get_mut(location_.archetype_id).value().get();
    // reuse header template wrapper by converting to vector and calling remove_by_ids via bundle::type_id_view
    remove_by_ids(archetype.components());
}

void EntityWorldMut::despawn() {
    assert_not_despawned();
    auto& entities  = world_->entities_mut();
    auto& archetype = world_->archetypes_mut().get_mut(location_.archetype_id).value().get();
    auto& table     = world_->storage_mut().tables.get_mut(archetype.table_id()).value().get();
    world_->trigger_on_despawn(archetype, entity_, archetype.components());
    world_->trigger_on_remove(archetype, entity_, archetype.components());
    location_ = world_->entities().get(entity_).value();
    world_->entities_mut().free(entity_);
    world_->flush_entities();
    auto result = archetype.swap_remove(location_.archetype_idx);
    if (result.swapped_entity) {
        auto swapped_entity            = result.swapped_entity.value();
        auto swapped_location          = entities.get(swapped_entity).value();
        swapped_location.archetype_idx = location_.archetype_idx;
        entities.set(swapped_entity.index, swapped_location);
    }
    auto table_result = table.swap_remove(result.table_row);
    if (table_result) {
        auto swapped_entity        = table_result.value();
        auto swapped_location      = entities.get(swapped_entity).value();
        swapped_location.table_idx = result.table_row;
        entities.set(swapped_entity.index, swapped_location);
        auto& swapped_archetype = world_->archetypes_mut().get_mut(swapped_location.archetype_id).value().get();
        swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
    }
    for (auto&& type_id : archetype.sparse_components()) {
        world_->storage_mut().sparse_sets.get_mut(type_id).and_then([&](storage::ComponentSparseSet& cs) {
            cs.remove(entity_);
            return std::optional<bool>(true);
        });
    }
    world_->flush();
}

}  // namespace epix::core

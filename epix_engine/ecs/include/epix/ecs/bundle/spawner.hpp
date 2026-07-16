#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
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

#include <epix/ecs/bundle/info.hpp>
#include <epix/ecs/detail/world_access.hpp>

namespace epix::ecs::internal {

// Bundle spawner for insert entities newly spawned (that is, not actually added to any archetype yet, otherwise will
// adding a 'move from empty archetype to target archetype' operation)
struct BundleSpawner {
   public:
    static BundleSpawner create_with_id(World& world, BundleId bundle_id, Tick tick);
    template <is_bundle T>
    static BundleSpawner create(World& world, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        auto registrator   = world_registrator(world);
        BundleId bundle_id = bundles.register_info<T>(registrator, world_storage_mut(world));
        return create_with_id(world, bundle_id, tick);
    }

    void reserve_storage(std::size_t additional);
    EntityLocation spawn_non_exist(Entity entity, is_bundle auto&& bundle) {
        auto& bundle_info = *bundle_info_;
        auto& archetype   = *archetype_;
        auto& table       = *table_;
        TableRow row      = table.allocate(entity);
        auto location     = archetype.allocate(entity, row);
        world_entities_mut(*world_).set(entity.index, location);
        auto spawn_bundle_status = std::views::take(std::views::repeat(ComponentStatus::Added),
                                                    std::ranges::size(bundle_info.explicit_components()));
        bundle_info.write_components(table, world_storage_mut(*world_).sparse_sets, world_components(*world_),
                                     spawn_bundle_status, bundle_info.required_component_constructors(), entity, row,
                                     change_tick_, bundle, InsertMode::Replace);
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
}  // namespace epix::ecs::internal

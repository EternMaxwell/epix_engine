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
#include <epix/ecs/world/decl.hpp>

namespace epix::ecs::internal {

// Bundle spawner for insert entities newly spawned (that is, not actually added to any archetype yet, otherwise will
// adding a 'move from empty archetype to target archetype' operation)
struct BundleSpawner {
   public:
    static BundleSpawner create_with_id(World& world, BundleId bundle_id, Tick tick);
    template <is_bundle T>
    static BundleSpawner create(World& world, Tick tick) {
        auto& bundles = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, bundle_id, tick);
    }

    void reserve_storage(std::size_t additional);
    EntityLocation spawn_non_exist(Entity entity, BundleRef bundle);

   private:
    World* world_ = nullptr;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Table* table_;
    Tick change_tick_;
};
}  // namespace epix::ecs::internal
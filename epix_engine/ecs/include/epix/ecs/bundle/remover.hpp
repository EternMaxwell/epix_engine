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
struct BundleRemover {
   public:
    static BundleRemover create_with_id(World& world, ArchetypeId archetype_id, BundleId bundle_id, Tick tick);

    template <is_bundle T>
    static BundleRemover create(World& world, ArchetypeId archetype_id, Tick tick) {
        auto& bundles = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, archetype_id, bundle_id, tick);
    }
    static BundleRemover create_with_type_id(World& world, ArchetypeId archetype_id, TypeId type_id, Tick tick);

    EntityLocation remove(Entity entity, EntityLocation location);

   private:
    World* world_ = nullptr;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Archetype* new_archetype_;
    Table* table_;
    Tick change_tick_;
};
}  // namespace epix::ecs::internal

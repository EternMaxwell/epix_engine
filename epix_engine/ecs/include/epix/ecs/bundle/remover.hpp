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
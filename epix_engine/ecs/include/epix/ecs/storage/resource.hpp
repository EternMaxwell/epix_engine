#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#endif

#include <epix/ecs/component/components.hpp>
#include <epix/ecs/storage/sparse_set.hpp>

namespace epix::ecs {
EPIX_EXPORT struct World;

/** Marks the canonical entity that owns a resource component. */
EPIX_EXPORT struct IsResource {
   public:
    explicit IsResource(TypeId resource_component_id) noexcept : resource_component_id_(resource_component_id) {}

    TypeId resource_component_id() const noexcept { return resource_component_id_; }

    static void on_insert(World& world, HookContext context);
    static void on_remove(World& world, HookContext context);
    static void on_despawn(World& world, HookContext context);

   private:
    TypeId resource_component_id_;
};

/** Cache from a resource component id to its canonical entity. */
EPIX_EXPORT struct ResourceEntities {
    std::size_t size() const noexcept { return entities.size(); }
    bool empty() const noexcept { return entities.empty(); }
    auto iter() const noexcept { return entities.iter(); }
    void clear() { entities.clear(); }
    bool contains(TypeId resource_id) const noexcept { return entities.contains(resource_id.get()); }
    std::optional<Entity> get(TypeId resource_id) const noexcept {
        return entities.get(resource_id.get()).transform([](const Entity& entity) { return entity; });
    }
    void insert(TypeId resource_id, Entity entity) { entities.emplace(resource_id.get(), entity); }
    bool remove(TypeId resource_id) { return entities.remove(resource_id.get()); }

   private:
    SparseSet<std::size_t, Entity> entities;
};
}  // namespace epix::ecs

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "entities.hpp"
#include "fwd.hpp"
#include "storage/sparse_set.hpp"
#include "storage/table.hpp"
#include "type_system/type_registry.hpp"

namespace epix::core {
struct HookContext {
    Entity entity;
    size_t component_id;
};
struct ComponentHooks {
    std::function<void(World&, HookContext)> on_add;
    std::function<void(World&, HookContext)> on_insert;
    std::function<void(World&, HookContext)> on_replace;
    std::function<void(World&, HookContext)> on_remove;
    std::function<void(World&, HookContext)> on_despawn;

    template <typename T>
    ComponentHooks& update_from_component() {
        if constexpr (requires(World& world, HookContext ctx) { T::on_add(world, ctx); }) {
            on_add = T::on_add;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_insert(world, ctx); }) {
            on_insert = T::on_insert;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_replace(world, ctx); }) {
            on_replace = T::on_replace;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_remove(world, ctx); }) {
            on_remove = T::on_remove;
        }
        if constexpr (requires(World& world, HookContext ctx) { T::on_despawn(world, ctx); }) {
            on_despawn = T::on_despawn;
        }
        return *this;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_add(F&& f) {
        if (on_add) {
            on_add = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_insert(F&& f) {
        if (on_insert) {
            on_insert = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_replace(F&& f) {
        if (on_replace) {
            on_replace = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_remove(F&& f) {
        if (on_remove) {
            on_remove = std::forward<F>(f);
            return true;
        }
        return false;
    }
    template <std::invocable<World&, HookContext> F>
    bool try_on_despawn(F&& f) {
        if (on_despawn) {
            on_despawn = std::forward<F>(f);
            return true;
        }
        return false;
    }
};
struct RequiredComponent {
    // function to construct the component for the target entity, the size_t is the component index inside table.
    std::function<void(storage::Tables&, storage::SparseSets&, Tick, size_t, Entity)> constructor;
    uint16_t inheritance_depth = 0;
};
struct RequiredComponents {
    std::unordered_map<size_t, RequiredComponent> components;
};
enum class StorageType : uint8_t {
    Table     = 0,  // default stored in tables
    SparseSet = 1,
};
template <typename T>
StorageType get_storage_type_for() {
    if constexpr (requires {
                      { T::storage_type() } -> std::same_as<StorageType>;
                  }) {
        return T::storage_type();
    } else {
        return StorageType::Table;
    }
}
struct ComponentDesc {
   private:
    const type_system::TypeInfo* _type_info;
    StorageType _storage_type;

    ComponentDesc(const type_system::TypeInfo* type_info, StorageType storage_type)
        : _type_info(type_info), _storage_type(storage_type) {}

   public:
    template <typename T>
    static ComponentDesc from_type() {
        return ComponentDesc(type_system::TypeInfo::get_info<T>(), get_storage_type_for<T>());
    }
    static ComponentDesc from_info(const type_system::TypeInfo* type_info, StorageType storage_type) {
        return ComponentDesc(type_info, storage_type);
    }

    const type_system::TypeInfo* type_info() const { return _type_info; }
    StorageType storage_type() const { return _storage_type; }
};
struct ComponentInfo {
   private:
    size_t _id;
    ComponentDesc _desc;
    ComponentHooks _hooks;
    RequiredComponents _required_components;
    std::unordered_set<size_t> _required_by;

   public:
    ComponentInfo(size_t id, const ComponentDesc& desc) : _id(id), _desc(desc) {}

    size_t type_id() const { return _id; }
    const type_system::TypeInfo* type_info() const { return _desc.type_info(); }
    StorageType storage_type() const { return _desc.storage_type(); }
    const ComponentHooks& hooks() const { return _hooks; }
    const RequiredComponents& required_components() const { return _required_components; }
};
struct Components : public storage::SparseSet<size_t, ComponentInfo> {
    using storage::SparseSet<size_t, ComponentInfo>::SparseSet;
};
}  // namespace epix::core
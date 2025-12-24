#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
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
    TypeId component_id;
};
/// Component Hooks struct for storing component lifecycle hooks
/// the priority of the hooks if can be called at the same time is as follows:
/// [on_despawn > on_replace > on_remove] and [on_add > on_insert]
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

// function to construct the component for the target entity, the size_t is the component index inside table.
using RequiredComponentConstructor =
    std::shared_ptr<std::function<void(storage::Table&, storage::SparseSets&, Tick, TableRow, Entity)>>;
struct RequiredComponent {
    RequiredComponentConstructor constructor;
    uint16_t inheritance_depth = 0;
};
struct RequiredComponents {
    std::unordered_map<TypeId, RequiredComponent> components;

    void register_dynamic(TypeId type_id, uint32_t inheritance_depth, RequiredComponentConstructor constructor) {
        // replace if exists.
        auto it = components.find(type_id);
        if (it == components.end() || inheritance_depth < it->second.inheritance_depth) {
            components[type_id] = RequiredComponent{.constructor       = std::move(constructor),
                                                    .inheritance_depth = static_cast<uint16_t>(inheritance_depth)};
        }
    }
    template <typename C, typename F>
    void register_id(TypeId type_id, uint32_t inheritance_depth, F&& constructor)
        requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>
    {
        register_dynamic(type_id, inheritance_depth,
                         RequiredComponentConstructor([ctor = std::forward<F>(constructor), type_id](
                                                          storage::Table& table, storage::SparseSets& sparse_sets,
                                                          Tick tick, TableRow row, Entity entity) {
                             auto storage_type = storage_for<C>();
                             if (storage_type == StorageType::Table) {
                                 auto& dense = table.get_dense_mut(type_id).value().get();
                                 if (row.get() < dense.len()) {
                                     dense.resize_uninitialized(row.get() + 1);
                                     dense.initialize_emplace<C>(row, {tick, tick}, ctor());
                                 } else {
                                     dense.replace<C>(row, tick, ctor());
                                 }
                             } else {
                                 auto& sparse_set = sparse_sets.get_mut(type_id).value().get();
                                 sparse_set.emplace<C>(entity, ctor());
                             }
                         }));
    }
    template <typename R>
    void remove_range(R&& range)
        requires std::ranges::view<R> && std::same_as<std::ranges::range_value_t<R>, TypeId>
    {
        for (auto&& type_id : range) {
            components.erase(type_id);
        }
    }

    void merge(const RequiredComponents& other) {
        for (auto&& [idx, rc] : other.components) {
            register_dynamic(idx, rc.inheritance_depth, rc.constructor);
        }
    }
};
struct ComponentInfo {
   private:
    TypeId _id;
    const TypeInfo* _info;
    ComponentHooks _hooks;
    RequiredComponents _required_components;
    std::unordered_set<TypeId> _required_by;

   public:
    ComponentInfo(TypeId id, const TypeInfo* info) : _id(id), _info(info) {}

    TypeId type_id() const { return _id; }
    const type_system::TypeInfo* type_info() const { return _info; }
    StorageType storage_type() const { return _info->storage_type; }
    const ComponentHooks& hooks() const { return _hooks; }
    const RequiredComponents& required_components() const { return _required_components; }
    template <typename T>
    void update_hooks() {
        _hooks.update_from_component<T>();
    }

    friend struct Components;
};
struct Components : public storage::SparseSet<TypeId, ComponentInfo> {
   private:
    std::shared_ptr<type_system::TypeRegistry> type_registry;

   public:
    Components(std::shared_ptr<type_system::TypeRegistry> type_registry)
        : storage::SparseSet<TypeId, ComponentInfo>(), type_registry(std::move(type_registry)) {}
    const type_system::TypeRegistry& registry() const { return *type_registry; }
    template <typename T>
    TypeId register_info() {
        auto id = type_registry->type_id<T>();
        if (!contains(id)) {
            auto info = ComponentInfo(id, type_registry->type_info(id));
            info.update_hooks<T>();
            emplace(id, std::move(info));
        }
        return id;
    }
    template <typename F>
    void register_required_component(TypeId requiree, TypeId required, F&& constructor)
        requires std::invocable<F>
    {
        using C                   = std::invoke_result_t<F>;
        auto& required_components = get_mut(requiree).value().get()._required_components;
        if (required_components.components.contains(required)) return;
        required_components.register_id<C>(required, 0, std::forward<F>(constructor));
        auto& required_by = get_mut(required).value().get()._required_by;
        required_by.insert(requiree);

        RequiredComponents required_components_tmp;
        auto inherited_requirements =
            register_inherited_required_components(requiree, required, required_components_tmp);
        required_components.merge(required_components_tmp);

        required_by.insert_range(get(required).value().get()._required_by);
        for (auto&& required_by_id : get(requiree).value().get()._required_by) {
            auto& required_components = get_mut(required_by_id).value().get()._required_components;
            auto&& depth =
                get_mut(required_by_id).value().get()._required_components.components.at(requiree).inheritance_depth;
            required_components.register_id<C>(requiree, depth + 1, std::forward<F>(constructor));
            for (auto&& [type_id, req_comp] : inherited_requirements) {
                required_components.register_dynamic(type_id, req_comp.inheritance_depth + depth + 1,
                                                     req_comp.constructor);
                get_mut(type_id).value().get()._required_by.insert(required_by_id);
            }
        }
    }

   private:
    std::vector<std::pair<TypeId, RequiredComponent>> register_inherited_required_components(
        TypeId requiree, TypeId required, RequiredComponents& required_components) {
        auto& required_info = get_mut(required).value().get();
        std::vector<std::pair<TypeId, RequiredComponent>> inherited_requirements =
            required_info.required_components().components | std::views::transform([&](auto&& rc) {
                auto&& [type_id, req_comp] = rc;
                return std::pair(type_id,
                                 RequiredComponent{
                                     .constructor       = req_comp.constructor,
                                     .inheritance_depth = static_cast<uint16_t>(req_comp.inheritance_depth + 1),
                                 });
            }) |
            std::ranges::to<std::vector<std::pair<TypeId, RequiredComponent>>>();
        for (auto&& [type_id, req_comp] : inherited_requirements) {
            required_components.register_dynamic(type_id, req_comp.inheritance_depth, req_comp.constructor);
            get_mut(type_id).value().get()._required_by.insert(requiree);
        }

        return inherited_requirements;
    }
};
}  // namespace epix::core
module;

#include <cassert>

export module epix.core:component;

import std;

import :entities;
import :world.decl;
import :type_registry;
import :tick;
import :storage.sparse_set;
import :storage.table;

namespace epix::core {
/** @brief Context passed to component lifecycle hook callbacks.
 *  Contains the entity being affected and the component type involved. */
export struct HookContext {
    /** @brief The entity being affected. */
    Entity entity;
    /** @brief The type id of the component being hooked. */
    TypeId component_id;
};
/** @brief Stores component lifecycle hook function pointers.
 *  Priority when multiple hooks fire simultaneously:
 *  on_despawn > on_replace > on_remove > removed > added > on_add > on_insert. */
export struct ComponentHooks {
    using HookFunc = void (*)(World&, HookContext);

    HookFunc on_add     = nullptr;
    HookFunc on_insert  = nullptr;
    HookFunc on_replace = nullptr;
    HookFunc on_remove  = nullptr;
    HookFunc on_despawn = nullptr;

    /** @brief Populate hook function pointers from static member functions defined on type T.
     *  @tparam T The component type potentially defining on_add/on_insert/on_replace/on_remove/on_despawn. */
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
    /** @brief Set the on_add hook if not already set. @return true if it was set. */
    bool try_on_add(HookFunc func) {
        if (on_add) {
            on_add = func;
            return true;
        }
        return false;
    }
    /** @brief Set the on_insert hook if not already set. @return true if it was set. */
    bool try_on_insert(HookFunc func) {
        if (on_insert) {
            on_insert = func;
            return true;
        }
        return false;
    }
    /** @brief Set the on_replace hook if not already set. @return true if it was set. */
    bool try_on_replace(HookFunc func) {
        if (on_replace) {
            on_replace = func;
            return true;
        }
        return false;
    }
    /** @brief Set the on_remove hook if not already set. @return true if it was set. */
    bool try_on_remove(HookFunc func) {
        if (on_remove) {
            on_remove = func;
            return true;
        }
        return false;
    }
    /** @brief Set the on_despawn hook if not already set. @return true if it was set. */
    bool try_on_despawn(HookFunc func) {
        if (on_despawn) {
            on_despawn = func;
            return true;
        }
        return false;
    }
};

// function to construct the component for the target entity, the size_t is the component index inside table.
using RequiredComponentConstructor = std::shared_ptr<std::function<void(Table&, SparseSets&, Tick, TableRow, Entity)>>;
struct RequiredComponent {
    RequiredComponentConstructor constructor;
    std::uint16_t inheritance_depth = 0;
};
struct RequiredComponents {
    std::unordered_map<TypeId, RequiredComponent> components;

    void register_dynamic(TypeId type_id, std::uint32_t inheritance_depth, RequiredComponentConstructor constructor) {
        // replace if exists.
        auto it = components.find(type_id);
        if (it == components.end() || inheritance_depth < it->second.inheritance_depth) {
            components[type_id] = RequiredComponent{.constructor       = std::move(constructor),
                                                    .inheritance_depth = static_cast<std::uint16_t>(inheritance_depth)};
        }
    }
    template <typename C, typename F>
    void register_id(TypeId type_id, std::uint32_t inheritance_depth, F&& constructor)
        requires std::invocable<F> && std::same_as<C, std::invoke_result_t<F>>
    {
        register_dynamic(type_id, inheritance_depth,
                         RequiredComponentConstructor(
                             [ctor = std::forward<F>(constructor), type_id](Table& table, SparseSets& sparse_sets,
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
    ::epix::meta::type_index _index;
    StorageType _storage_type;
    ComponentHooks _hooks;
    RequiredComponents _required_components;
    std::unordered_set<TypeId> _required_by;

   public:
    ComponentInfo(TypeId id, ::epix::meta::type_index index, StorageType storage_type)
        : _id(id), _index(index), _storage_type(storage_type) {}

    /** @brief Get the component's type id. */
    TypeId type_id() const { return _id; }
    /** @brief Get the component's runtime type index. */
    ::epix::meta::type_index type_index() const { return _index; }
    /** @brief Get the component's storage type (Table or SparseSet). */
    StorageType storage_type() const { return _storage_type; }
    /** @brief Get the component's lifecycle hooks. */
    const ComponentHooks& hooks() const { return _hooks; }
    /** @brief Get the components that are automatically added when this component is inserted. */
    const RequiredComponents& required_components() const { return _required_components; }
    /** @brief Update lifecycle hooks from static members defined on type T.
     *  @tparam T The component type. */
    template <typename T>
    void update_hooks() {
        _hooks.update_from_component<T>();
    }

    friend struct Components;
};
/** @brief Central component registry that also manages required-component relationships.
 *  Inherits from SparseSet<TypeId, ComponentInfo>. */
export struct Components : public SparseSet<TypeId, ComponentInfo> {
   private:
    std::shared_ptr<TypeRegistry> type_registry;

   public:
    Components(std::shared_ptr<TypeRegistry> type_registry)
        : SparseSet<TypeId, ComponentInfo>(), type_registry(std::move(type_registry)) {}
    /** @brief Get a const reference to the underlying type registry. */
    const TypeRegistry& registry() const { return *type_registry; }
    /** @brief Register component info for type T, creating it if not already registered.
     *  @tparam T The component type.
     *  @return The assigned TypeId. */
    template <typename T>
    TypeId register_info() {
        auto id = type_registry->type_id<T>();
        if (!contains(id)) {
            auto info = ComponentInfo(id, type_registry->type_index(id), type_registry->storage_type(id));
            info.update_hooks<T>();
            emplace(id, std::move(info));
        }
        return id;
    }
    /** @brief Overwrite lifecycle hooks for the given component type.
     *  Only non-null hook pointers in `hooks` are applied. */
    void update_hooks(TypeId type_id, ComponentHooks hooks) {
        get_mut(type_id).transform([&](ComponentInfo& info) -> bool {
            if (hooks.on_add) {
                info._hooks.on_add = std::move(hooks.on_add);
            }
            if (hooks.on_insert) {
                info._hooks.on_insert = std::move(hooks.on_insert);
            }
            if (hooks.on_replace) {
                info._hooks.on_replace = std::move(hooks.on_replace);
            }
            if (hooks.on_remove) {
                info._hooks.on_remove = std::move(hooks.on_remove);
            }
            if (hooks.on_despawn) {
                info._hooks.on_despawn = std::move(hooks.on_despawn);
            }

            return true;
        });
    }
    /** @brief Register a required component for `requiree` using a typed constructor.
     *  @tparam F Invocable returning the required component value.
     *  @param requiree The component that requires another.
     *  @param constructor Factory producing the default value of the required component. */
    template <typename F>
    void register_required(TypeId requiree, F&& constructor)
        requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
    {
        auto required = registry().type_id<std::invoke_result_t<F>>();
        register_required_by_id(requiree, required, std::forward<F>(constructor));
    }
    /** @brief Register a required component by explicit TypeId pair.
     *  @param requiree Component that requires `required`.
     *  @param required The required component's TypeId.
     *  @param constructor Factory producing the default value. */
    template <typename F>
    void register_required_by_id(TypeId requiree, TypeId required, F&& constructor)
        requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
    {
        using C = std::invoke_result_t<F>;
        assert(required == registry().type_id<C>() && "required type must match the constructor return type");
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
    /** @brief Register a required component using a type-erased constructor.
     *  @param requiree Component that requires `required`.
     *  @param required The required component's TypeId.
     *  @param constructor Type-erased factory for the required component. */
    void register_required_dyn(TypeId requiree, TypeId required, RequiredComponentConstructor constructor) {
        auto& required_components = get_mut(requiree).value().get()._required_components;
        if (required_components.components.contains(required)) return;
        required_components.register_dynamic(required, 0, std::move(constructor));
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
            required_components.register_dynamic(requiree, depth + 1, constructor);
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
            std::ranges::to<std::vector<std::pair<TypeId, RequiredComponent>>>(std::views::transform(
                required_info.required_components().components,
                [&](auto&& rc) {
                    auto&& [type_id, req_comp] = rc;
                    return std::pair(type_id,
                                     RequiredComponent{
                                         .constructor       = req_comp.constructor,
                                         .inheritance_depth = static_cast<std::uint16_t>(req_comp.inheritance_depth + 1),
                                     });
                }));
        for (auto&& [type_id, req_comp] : inherited_requirements) {
            required_components.register_dynamic(type_id, req_comp.inheritance_depth, req_comp.constructor);
            get_mut(type_id).value().get()._required_by.insert(requiree);
        }

        return inherited_requirements;
    }
};

const Components& world_components(const World& world);
Components& world_components_mut(World& world);
}  // namespace core
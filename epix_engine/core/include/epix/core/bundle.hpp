#pragma once

#include <cstddef>
#include <format>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "archetype.hpp"
#include "component.hpp"
#include "entities.hpp"
#include "epix/core/archetype.hpp"
#include "fwd.hpp"
#include "meta/typeid.hpp"
#include "storage.hpp"
#include "storage/dense.hpp"
#include "storage/sparse_set.hpp"
#include "storage/table.hpp"
#include "type_system/type_registry.hpp"

namespace epix::core {
namespace bundle {
template <template <typename...> typename Templated, typename T>
struct is_specialization_of : std::false_type {};
template <template <typename...> typename Templated, typename... Ts>
struct is_specialization_of<Templated, Templated<Ts...>> : std::true_type {};
template <template <typename...> typename Templated, typename T>
concept specialization_of = is_specialization_of<Templated, T>::value;
template <typename V, typename T>
struct is_constructible_from_tuple : std::false_type {};
template <typename T, typename... Args>
struct is_constructible_from_tuple<T, std::tuple<Args...>> : std::bool_constant<std::is_constructible_v<T, Args...>> {};
template <typename V, typename T>
concept constructible_from_tuple = is_constructible_from_tuple<V, T>::value;

template <typename R>
concept is_void_ptr_view = requires(R r) {
    { std::ranges::view<R> };
    { std::ranges::sized_range<R> };
    { std::same_as<std::ranges::range_value_t<R>, void*> };
};

template <typename R>
concept type_id_view = requires(R r) {
    { std::ranges::view<R> };
    { std::ranges::sized_range<R> };
    { std::same_as<std::ranges::range_value_t<R>, type_system::TypeId> };
};
template <typename B>
concept is_bundle = requires(B b) {
    { b.write(std::declval<std::span<void*>>()) } -> std::same_as<void>;
    { B::type_ids(std::declval<const type_system::TypeRegistry&>()) } -> type_id_view;
    { B::register_components(std::declval<const type_system::TypeRegistry&>(), std::declval<Components&>()) };
};
}  // namespace bundle

using bundle::is_bundle;

template <typename T>
struct Bundle {};

/**
 * @brief Detail of a bundle type.
 * The first template parameter is a tuple of the types that will explicitly added to the entity by this bundle.
 * The second template parameter is a tuple of tuples, each inner tuple contains the argument types for constructing
 * the corresponding type in the first tuple.
 *
 * @tparam Ts
 * @tparam Args
 */
template <typename Ts, typename Args>
struct InitializeBundle {
    static_assert(false, "BundleDetail must be specialized for std::tuple types");
};
template <typename... Ts, typename... ArgTuples>
    requires((bundle::specialization_of<std::tuple, ArgTuples> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (bundle::constructible_from_tuple<Ts, ArgTuples> && ...))
struct InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> {
    // stores the args for constructing each component in Ts
    using storage_type = std::tuple<ArgTuples...>;
    storage_type args;

    /**
     * @brief Construct bundle types in place at the provided pointers.
     * The stored argument values should have lifetimes that extend beyond this call.
     * @param pointers
     */
    void write(bundle::is_void_ptr_view auto&& pointers) {
        assert(std::ranges::size(pointers) == sizeof...(Ts));
        auto&& ptr_it = std::ranges::begin(pointers);

        [&]<size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<size_t I>(std::integral_constant<size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    if (T* ptr = static_cast<T*>(*ptr_it)) {
                        []<size_t... Js>(std::index_sequence<Js...>, T* p, ATuple& atuple) {
                            new (p) T(std::forward<std::tuple_element_t<Js, ATuple>>(
                                std::get<Js>(atuple))...);  // construct T in place with args from atuple
                        }(std::make_index_sequence<std::tuple_size_v<ATuple>>{}, ptr, std::get<I>(args));
                    }
                    ++ptr_it;
                }(std::integral_constant<size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }

    static auto type_ids(const type_system::TypeRegistry& registry) {
        return std::array<type_system::TypeId, sizeof...(Ts)>{registry.type_id<Ts>()...};
    }
    static void register_components(const type_system::TypeRegistry& registry, Components& components) {
        (components.emplace(registry.type_id<Ts>(),
                            ComponentInfo(registry.type_id<Ts>(), ComponentDesc::from_type<Ts>())),
         ...);
    }
};
template <typename... Ts, typename... ArgTuples>
    requires((bundle::specialization_of<std::tuple, ArgTuples> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (bundle::constructible_from_tuple<Ts, ArgTuples> && ...))
InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> make_init_bundle(ArgTuples&&... args) {
    return InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>>{
        std::make_tuple(std::forward<ArgTuples>(args)...)};
}
struct BundleInfo {
   private:
    BundleId _id;
    std::vector<TypeId> _component_ids;  // explicit components followed by required components, explicit order matches
                                         // bundle write order
    std::vector<RequiredComponentConstructor> _required_components;
    size_t _explicit_components_count;

    BundleInfo(BundleId id,
               std::vector<TypeId> component_ids,
               std::vector<RequiredComponentConstructor> required_components,
               size_t explicit_components_count)
        : _id(id),
          _component_ids(std::move(component_ids)),
          _required_components(std::move(required_components)),
          _explicit_components_count(explicit_components_count) {}

   public:
    static BundleInfo create(
        std::string_view bundle_type_name,
        Storage& storage,
        const Components& components,
        bundle::type_id_view auto&& component_ids,  // should be the same order as bundle write and bundle type ids
        BundleId id) {
        auto deduped = component_ids | std::ranges::to<std::unordered_set<type_system::TypeId>>();
        if (deduped.size() != std::ranges::size(component_ids)) {
            auto seen  = std::unordered_set<type_system::TypeId>{};
            auto duped = component_ids | std::views::filter([&](type_system::TypeId tid) {
                             if (seen.contains(tid)) {
                                 return true;
                             } else {
                                 seen.insert(tid);
                                 return false;
                             }
                         });
            throw std::logic_error(std::format("bundle \"{}\" has duplicate component types {}", bundle_type_name,
                                               duped | std::views::transform([&](TypeId tid) {
                                                   return components.get(tid).value().get().type_info()->name;
                                               })));
        }

        size_t explicit_count = std::ranges::size(component_ids);
        RequiredComponents required_components;
        for (auto&& id : deduped) {
            const ComponentInfo& info = components.get(id).value().get();
            required_components.merge(info.required_components());
            storage.prepare_component(info);
        }
        auto comps                 = component_ids | std::ranges::to<std::vector>();
        auto required_constructors = required_components.components | std::views::filter([&](auto&& v) {
                                         auto&& [type_id, rc] = v;
                                         return !deduped.contains(type_id);
                                     }) |
                                     std::views::transform([&](auto&& v) {
                                         auto&& [type_id, rc] = v;
                                         storage.prepare_component(components.get(type_id).value().get());
                                         comps.push_back(type_id);
                                         return rc.constructor;
                                     }) |
                                     std::ranges::to<std::vector<RequiredComponentConstructor>>();

        return BundleInfo(id, comps, required_constructors, explicit_count);
    }

    BundleId id() const { return _id; }
    std::span<const TypeId> explicit_components() const {
        return std::span<const TypeId>(_component_ids.data(), _explicit_components_count);
    }
    std::span<const TypeId> required_components() const {
        return std::span<const TypeId>(_component_ids.data() + _explicit_components_count,
                                       _component_ids.size() - _explicit_components_count);
    }
    std::span<const TypeId> all_components() const { return _component_ids; }
    std::span<const RequiredComponentConstructor> required_component_constructors() const {
        return _required_components;
    }

    template <typename T1, typename T2, typename T3>
    void write_components(
        storage::Table& table,  // The table at row should be previously allocated, either existing or uninitialized
        storage::SparseSets& sparse_sets,
        const type_system::TypeRegistry& type_registry,
        const Components& components,
        T1&& component_statuses,   // status of each explicit component
        T2&& required_components,  // the required component constructors for required components needed to be added
        Entity entity,
        TableRow row,
        Tick tick,
        T3&& bundle,
        bool replace_existing = true) const
        requires std::ranges::view<std::decay_t<T2>> &&
                 std::same_as<std::ranges::range_value_t<T2>, RequiredComponentConstructor> &&
                 std::ranges::view<std::decay_t<T1>> && std::same_as<std::ranges::range_value_t<T1>, ComponentStatus> &&
                 bundle::is_bundle<std::decay_t<T3>>
    {
        // debug assert check whether bundle types match explicit component ids
        assert(std::ranges::all_of(std::views::zip(bundle.type_ids(type_registry), explicit_components()),
                                   [](auto&& pair) {
                                       auto&& [bundle_id, explicit_id] = pair;
                                       return bundle_id == explicit_id;
                                   }) &&
               std::ranges::size(bundle.type_ids(type_registry)) == std::ranges::size(explicit_components()));
        std::vector<void*> pointers;
        pointers.reserve(_explicit_components_count);
        for (auto&& [type_id, status] : std::views::zip(explicit_components(), component_statuses)) {
            auto storage_type = components.get(type_id).value().get().storage_type();
            if (storage_type == StorageType::Table) {
                storage::Dense& dense = table.get_dense_mut(type_id).value().get();
                void* ptr             = dense.get_mut(row).value();
                if (status == ComponentStatus::Added) {
                    dense.resize_uninitialized(row.get() + 1);  // in case the row was not previously allocated
                    pointers.push_back(ptr);
                    dense.get_added_tick(row).value().get()    = tick;
                    dense.get_modified_tick(row).value().get() = tick;
                } else if (replace_existing) {
                    // manually destroy existing component before replacing
                    dense.type_info()->destroy(ptr);
                    pointers.push_back(ptr);
                    dense.get_modified_tick(row).value().get() = tick;
                } else {
                    // keep existing, do nothing
                    pointers.push_back(nullptr);
                }
            } else {
                storage::ComponentSparseSet& sparse_set = sparse_sets.get_mut(type_id).value().get();
                assert(((status == ComponentStatus::Added) == !sparse_set.contains(entity)));
                if (status == ComponentStatus::Added) {
                    sparse_set.alloc_uninitialized(entity);
                    void* ptr = sparse_set.get_mut(entity).value();
                    pointers.push_back(ptr);
                    sparse_set.get_added_tick(entity).value().get()    = tick;
                    sparse_set.get_modified_tick(entity).value().get() = tick;
                } else if (replace_existing) {
                    // manually destroy existing component before replacing
                    void* ptr = sparse_set.get_mut(entity).value();
                    sparse_set.type_info()->destroy(ptr);
                    pointers.push_back(ptr);
                    sparse_set.get_modified_tick(entity).value().get() = tick;
                } else {
                    // keep existing, do nothing
                    pointers.push_back(nullptr);
                }
            }
        }
        bundle.write(pointers);

        for (auto&& rc : required_components) {
            (*rc)(table, sparse_sets, tick, row, entity);
        }
    }

    ArchetypeId insert_bundle_into_archetype(archetype::Archetypes& archetypes,
                                             Storage& storage,
                                             const Components& components,
                                             ArchetypeId archetype_id) const noexcept {
        if (auto&& opt = archetypes.get(archetype_id)
                             .and_then([&](std::reference_wrapper<const Archetype> arch) -> std::optional<ArchetypeId> {
                                 return arch.get().edges().get_archetype_after_bundle_insert(_id);
                             })) {
            return opt.value();
        }

        std::vector<TypeId> new_table_components;
        std::vector<TypeId> new_sparse_components;
        std::vector<ComponentStatus> component_status;
        std::vector<RequiredComponentConstructor> added_required_components;
        std::vector<TypeId> added_components;
        std::vector<TypeId> existing_components;

        auto& archetype = archetypes.get_mut(archetype_id).value().get();
        for (auto&& type_id : explicit_components()) {
            if (archetype.contains(type_id)) {
                existing_components.push_back(type_id);
                component_status.push_back(ComponentStatus::Exists);  // already exists
            } else {
                added_components.push_back(type_id);
                component_status.push_back(ComponentStatus::Added);
                auto storage_type = components.get(type_id).value().get().storage_type();
                if (storage_type == StorageType::Table) {
                    new_table_components.push_back(type_id);
                } else {
                    new_sparse_components.push_back(type_id);
                }
            }
        }

        for (auto&& [index, type_id] : required_components() | std::views::enumerate) {
            if (archetype.contains(type_id)) {
                // already exists
                continue;
            }
            added_required_components.push_back(_required_components[index]);
            added_components.push_back(type_id);
            auto storage_type = components.get(type_id).value().get().storage_type();
            if (storage_type == StorageType::Table) {
                new_table_components.push_back(type_id);
            } else {
                new_sparse_components.push_back(type_id);
            }
        }

        if (new_table_components.empty() && new_sparse_components.empty()) {
            // no new components added, the archetype remains the same
            archetype.edges_mut().cache_archetype_after_bundle_insert(
                _id, archetype_id, component_status, added_required_components, added_components, existing_components);
            return archetype_id;
        } else {
            // different archetype.
            TableId new_table_id;
            std::vector<TypeId> table_components;
            if (new_table_components.empty()) {
                new_table_id     = archetype.table_id();
                table_components = archetype.table_components() | std::ranges::to<std::vector>();
            } else {
                new_table_components.insert_range(new_table_components.end(), archetype.table_components());
                std::sort(new_table_components.begin(), new_table_components.end());
                new_table_id     = storage.tables.get_id_or_insert(new_table_components);
                table_components = std::move(new_table_components);
            }
            std::vector<TypeId> sparse_components = std::move(new_sparse_components);
            sparse_components.insert_range(sparse_components.end(), archetype.sparse_components());
            std::sort(sparse_components.begin(), sparse_components.end());

            ArchetypeId new_archetype_id =
                archetypes.get_id_or_insert(components, new_table_id, table_components, sparse_components).first;

            auto& archetype = archetypes.get_mut(archetype_id).value().get();
            archetype.edges_mut().cache_archetype_after_bundle_insert(_id, new_archetype_id, component_status,
                                                                      added_required_components, added_components,
                                                                      existing_components);
            return new_archetype_id;
        }
    }
    std::optional<ArchetypeId> remove_bundle_from_archetype(archetype::Archetypes& archetypes,
                                                            Storage& storage,
                                                            const Components& components,
                                                            ArchetypeId archetype_id,
                                                            bool ignore_missing) const noexcept {
        {
            auto& edges = archetypes.get_mut(archetype_id).value().get().edges();
            auto&& opt  = ignore_missing ? edges.get_archetype_after_bundle_remove(_id)
                                         : edges.get_archetype_after_bundle_take(_id);
            if (opt) return *opt;
        }
        // Not cached.
        std::vector<TypeId> next_table_components;
        std::vector<TypeId> next_sparse_components;
        TableId next_table_id;
        auto& archetype = archetypes.get_mut(archetype_id).value().get();
        {
            std::unordered_set<TypeId> table_components_set =
                archetype.table_components() | std::ranges::to<std::unordered_set<TypeId>>();
            std::unordered_set<TypeId> sparse_components_set =
                archetype.sparse_components() | std::ranges::to<std::unordered_set<TypeId>>();
            bool table_changed = false;
            for (auto&& type_id : explicit_components()) {
                if (archetype.contains(type_id)) {
                    // only remove if it exists in the archetype
                    auto storage_type = components.get(type_id).value().get().storage_type();
                    if (storage_type == StorageType::Table) {
                        table_components_set.erase(type_id);
                        table_changed = true;
                    } else {
                        sparse_components_set.erase(type_id);
                    }
                } else if (!ignore_missing) {
                    archetype.edges_mut().cache_archetype_after_bundle_take(_id, std::nullopt);
                    return std::nullopt;
                }
            }
            next_table_components  = table_components_set | std::ranges::to<std::vector>();
            next_sparse_components = sparse_components_set | std::ranges::to<std::vector>();
            if (!table_changed) {
                next_table_id = archetype.table_id();
            } else {
                std::sort(next_table_components.begin(), next_table_components.end());
                next_table_id = storage.tables.get_id_or_insert(next_table_components);
            }
        }
        ArchetypeId next_archetype_id =
            archetypes.get_id_or_insert(components, next_table_id, next_table_components, next_sparse_components).first;
        if (ignore_missing) {
            // remove
            archetype.edges_mut().cache_archetype_after_bundle_remove(_id, next_archetype_id);
        } else {
            // take
            archetype.edges_mut().cache_archetype_after_bundle_take(_id, next_archetype_id);
        }
        return next_archetype_id;
    }
};
struct Bundles {
   public:
    size_t size() const { return _bundle_infos.size(); }
    bool empty() const { return _bundle_infos.empty(); }
    auto iter() const { return std::views::all(_bundle_infos); }
    std::optional<std::reference_wrapper<const BundleInfo>> get(BundleId id) const {
        if (id.get() >= _bundle_infos.size()) {
            return std::nullopt;
        }
        return std::cref(_bundle_infos[id.get()]);
    }
    std::optional<BundleId> get_id(TypeId type_id) const {
        if (auto it = _dynamic_component_ids.find(type_id); it != _dynamic_component_ids.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    template <bundle::is_bundle T>
    BundleId register_info(const type_system::TypeRegistry& type_registry, Components& components, Storage& storage) {
        auto type_id = type_registry.type_id<T>();
        if (auto it = _bundle_ids.find(type_id); it != _bundle_ids.end()) {
            // already registered
            return it->second;
        }
        T::register_components(type_registry, components);
        BundleId new_id = static_cast<BundleId>(_bundle_infos.size());
        auto info =
            BundleInfo::create(meta::type_id<T>().name(), storage, components, T::type_ids(type_registry), new_id);
        _bundle_infos.emplace_back(std::move(info));
        return new_id;
    }
    template <bundle::is_bundle T>
    BundleId register_contributed_info(const type_system::TypeRegistry& type_registry,
                                       Components& components,
                                       Storage& storage) {
        auto type_id = type_registry.type_id<T>();
        if (auto it = _contributed_bundle_ids.find(type_id); it != _contributed_bundle_ids.end()) {
            // already registered
            return it->second;
        }
        BundleId explicit_id = register_info<T>(type_registry, components, storage);
        BundleId dyn_id      = init_dynamic_info(storage, components, _bundle_infos[explicit_id].all_components());
        _contributed_bundle_ids.emplace(type_id, dyn_id);
        return dyn_id;
    }

    BundleId init_dynamic_info(Storage& storage,
                               const Components& components,
                               bundle::type_id_view auto&& component_ids) {
        std::vector<TypeId> ids = component_ids | std::ranges::to<std::vector>();
        if (auto it = _dynamic_bundle_ids.find(ids); it != _dynamic_bundle_ids.end()) {
            return it->second;
        } else {
            BundleId new_id                   = static_cast<BundleId>(_bundle_infos.size());
            std::vector<StorageType> storages = ids | std::views::transform([&](TypeId type_id) {
                                                    return components.get(type_id).value().get().storage_type();
                                                }) |
                                                std::ranges::to<std::vector<StorageType>>();
            _dynamic_bundle_storages.emplace(new_id, std::move(storages));
            _dynamic_bundle_ids.emplace(std::move(ids), new_id);
            return new_id;
        }
    }

    BundleId init_component_info(Storage& storage, const Components& components, TypeId type_id) {
        if (auto it = _dynamic_component_ids.find(type_id); it != _dynamic_component_ids.end()) {
            return it->second;
        } else {
            BundleId new_id          = static_cast<BundleId>(_bundle_infos.size());
            StorageType storage_type = components.get(type_id).value().get().storage_type();
            _dynamic_component_storages.emplace(new_id, storage_type);
            _dynamic_component_ids.emplace(type_id, new_id);
            return new_id;
        }
    }

    // Get the storage type of a single-component dynamic bundle
    std::optional<StorageType> get_storage(BundleId id) const {
        if (auto it = _dynamic_component_storages.find(id); it != _dynamic_component_storages.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    // Get the storage types of a multi-component dynamic bundle
    std::optional<std::span<const StorageType>> get_storages(BundleId id) const {
        if (auto it = _dynamic_bundle_storages.find(id); it != _dynamic_bundle_storages.end()) {
            return std::span<const StorageType>(it->second);
        }
        return std::nullopt;
    }

   private:
    std::vector<BundleInfo> _bundle_infos;
    std::unordered_map<TypeId, BundleId> _bundle_ids;
    std::unordered_map<TypeId, BundleId> _contributed_bundle_ids;

    std::unordered_map<std::vector<TypeId>, BundleId, storage::VecHash> _dynamic_bundle_ids;
    std::unordered_map<BundleId, std::vector<StorageType>, std::hash<size_t>> _dynamic_bundle_storages;

    // Cache for optimizing single component bundles
    std::unordered_map<TypeId, BundleId> _dynamic_component_ids;
    std::unordered_map<BundleId, StorageType, std::hash<size_t>> _dynamic_component_storages;
};

struct BundleInserter {
   public:
    static BundleInserter create_with_id(Bundles& bundles,
                                         Entities& entities,
                                         archetype::Archetypes& archetypes,
                                         Storage& storage,
                                         Components& components,
                                         const type_system::TypeRegistry& type_registry,
                                         ArchetypeId archetype_id,
                                         BundleId bundle_id,
                                         Tick tick) {
        auto&& bundle_info = bundles.get(bundle_id).value().get();
        ArchetypeId new_archetype_id =
            bundle_info.insert_bundle_into_archetype(archetypes, storage, components, archetype_id);
        Archetype& archetype = archetypes.get_mut(archetype_id).value().get();
        BundleInserter inserter;
        inserter.bundles_       = &bundles;
        inserter.entities_      = &entities;
        inserter.archetypes_    = &archetypes;
        inserter.storage_       = &storage;
        inserter.components_    = &components;
        inserter.type_registry_ = &type_registry;
        inserter.archetype_after_insert_ =
            &archetype.edges().get_archetype_after_bundle_insert_detail(bundle_id).value().get();
        inserter.bundle_info_ = &bundle_info;
        inserter.archetype_   = &archetype;
        inserter.table_       = &storage.tables.get_mut(archetype.table_id()).value().get();
        inserter.change_tick_ = tick;
        return inserter;
    }
    template <bundle::is_bundle T>
    static BundleInserter create(Bundles& bundles,
                                 Entities& entities,
                                 archetype::Archetypes& archetypes,
                                 Storage& storage,
                                 Components& components,
                                 const type_system::TypeRegistry& type_registry,
                                 ArchetypeId archetype_id,
                                 Tick tick) {
        BundleId bundle_id = bundles.register_info<T>(type_registry, components, storage);
        return create_with_id(bundles, entities, archetypes, storage, components, type_registry, archetype_id,
                              bundle_id, tick);
    }

    template <typename T>
    EntityLocation insert(Entity entity, EntityLocation location, T&& bundle, bool replace_existing = true) const
        requires bundle::is_bundle<std::decay_t<T>>
    {
        auto& bundle_info         = *bundle_info_;
        auto& dest_archetype      = archetypes_->get_mut(archetype_after_insert_->archetype_id).value().get();
        const bool same_archetype = (archetype_->id() == dest_archetype.id());
        const bool same_table     = (archetype_->table_id() == dest_archetype.table_id());
        if (same_archetype) {
            // same archetype, just write components in place
            bundle_info.write_components(*table_, storage_->sparse_sets, *type_registry_, *components_,
                                         archetype_after_insert_->iter_status(),
                                         archetype_after_insert_->required_components | std::views::all, entity,
                                         location.table_idx, change_tick_, bundle, replace_existing);
            // location not changed
            return location;
        } else if (same_table) {
            // table not changed, but archetype changed due to sparse components
            auto result = archetype_->swap_remove(location.archetype_idx);
            if (result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity            = result.swapped_entity.value();
                auto swapped_location          = entities_->get(swapped_entity).value();
                swapped_location.archetype_idx = location.archetype_idx;
                entities_->set(swapped_entity.index, swapped_location);
            }
            auto new_location = dest_archetype.allocate(entity, result.table_row);
            entities_->set(entity.index, new_location);
            bundle_info.write_components(*table_, storage_->sparse_sets, *type_registry_, *components_,
                                         archetype_after_insert_->iter_status(),
                                         archetype_after_insert_->required_components | std::views::all, entity,
                                         result.table_row, change_tick_, bundle, replace_existing);
            return new_location;
        } else {
            auto& new_table = storage_->tables.get_mut(dest_archetype.table_id()).value().get();
            auto& table     = *table_;
            auto result     = archetype_->swap_remove(location.archetype_idx);
            if (result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity            = result.swapped_entity.value();
                auto swapped_location          = entities_->get(swapped_entity).value();
                swapped_location.archetype_idx = location.archetype_idx;
                entities_->set(swapped_entity.index, swapped_location);
            }
            auto move_result  = table.move_to(result.table_row, new_table);
            auto new_location = dest_archetype.allocate(entity, move_result.new_index);
            entities_->set(entity.index, new_location);
            if (move_result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity        = move_result.swapped_entity.value();
                auto swapped_location      = entities_->get(swapped_entity).value();
                swapped_location.table_idx = result.table_row;
                entities_->set(swapped_entity.index, swapped_location);
                auto& swapped_archetype = archetypes_->get_mut(swapped_location.archetype_id).value().get();
                swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
            }
            bundle_info.write_components(new_table, storage_->sparse_sets, *type_registry_, *components_,
                                         archetype_after_insert_->iter_status(),
                                         archetype_after_insert_->required_components | std::views::all, entity,
                                         move_result.new_index, change_tick_, bundle, replace_existing);
            return new_location;
        }
    }

   private:
    Bundles* bundles_;
    Entities* entities_;
    archetype::Archetypes* archetypes_;
    Storage* storage_;
    Components* components_;
    const type_system::TypeRegistry* type_registry_;
    const archetype::ArchetypeAfterBundleInsert* archetype_after_insert_;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    storage::Table* table_;
    Tick change_tick_;
};

// Bundle spawner for insert entities newly spawned (that is, not actually added to any archetype yet, otherwise will
// adding a 'move from empty archetype to target archetype' operation)
struct BundleSpawner {
   public:
    static BundleSpawner create_with_id(Bundles& bundles,
                                        Entities& entities,
                                        archetype::Archetypes& archetypes,
                                        Storage& storage,
                                        Components& components,
                                        const type_system::TypeRegistry& type_registry,
                                        BundleId bundle_id,
                                        Tick tick) {
        auto&& bundle_info = bundles.get(bundle_id).value().get();
        ArchetypeId new_archetype_id =
            bundle_info.insert_bundle_into_archetype(archetypes, storage, components, ArchetypeId(0));
        Archetype& archetype = archetypes.get_mut(new_archetype_id).value().get();
        BundleSpawner spawner;
        spawner.bundles_       = &bundles;
        spawner.entities_      = &entities;
        spawner.archetypes_    = &archetypes;
        spawner.storage_       = &storage;
        spawner.components_    = &components;
        spawner.type_registry_ = &type_registry;
        spawner.bundle_info_   = &bundle_info;
        spawner.archetype_     = &archetype;
        spawner.table_         = &storage.tables.get_mut(archetype.table_id()).value().get();
        spawner.change_tick_   = tick;
        return spawner;
    }
    template <bundle::is_bundle T>
    static BundleSpawner create(Bundles& bundles,
                                Entities& entities,
                                archetype::Archetypes& archetypes,
                                Storage& storage,
                                Components& components,
                                const type_system::TypeRegistry& type_registry,
                                Tick tick) {
        BundleId bundle_id = bundles.register_info<T>(type_registry, components, storage);
        return create_with_id(bundles, entities, archetypes, storage, components, type_registry, bundle_id, tick);
    }

    void reserve_storage(size_t additional) {
        if (additional == 0) return;
        auto& table     = *table_;
        auto& archetype = *archetype_;
        table.reserve(table.size() + additional);
        archetype.reserve(archetype.size() + additional);
    }
    template <typename T>
    EntityLocation spawn_non_exist(Entity entity, T&& bundle)
        requires bundle::is_bundle<std::decay_t<T>>
    {
        auto& bundle_info = *bundle_info_;
        auto& archetype   = *archetype_;
        auto& table       = *table_;
        TableRow row      = table.allocate(entity);
        auto location     = archetype.allocate(entity, row);
        entities_->set(entity.index, location);
        auto spawn_bundle_status = std::views::repeat(ComponentStatus::Added) |
                                   std::views::take(std::ranges::size(bundle_info.explicit_components()));
        bundle_info.write_components(table, storage_->sparse_sets, *type_registry_, *components_, spawn_bundle_status,
                                     bundle_info.required_component_constructors(), entity, row, change_tick_, bundle,
                                     true);
        return location;
    }

   private:
    Bundles* bundles_;
    Entities* entities_;
    archetype::Archetypes* archetypes_;
    Storage* storage_;
    Components* components_;
    const type_system::TypeRegistry* type_registry_;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    storage::Table* table_;
    Tick change_tick_;
};
}  // namespace epix::core
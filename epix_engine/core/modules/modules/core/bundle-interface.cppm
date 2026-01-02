module;

#include <cassert>

export module epix.core:bundle.interface;

import std;
import epix.traits;

import :utils;
import :type_registry;
import :component;
import :storage;
import :archetype;

namespace core {
export template <typename T>
struct Bundle {};

template <typename R>
concept is_void_ptr_view = std::ranges::sized_range<R> && view_of_value<R, void*>;

template <typename B>
concept is_bundle = requires(std::decay_t<B>& b) {
    {
        Bundle<std::decay_t<B>>::write(b, std::declval<std::span<void*>>())
    } -> std::same_as<std::size_t>;  // return number of written components
    { Bundle<std::decay_t<B>>::type_ids(std::declval<const TypeRegistry&>()) } -> type_id_view;
    { Bundle<std::decay_t<B>>::register_components(std::declval<const TypeRegistry&>(), std::declval<Components&>()) };
};

struct BundleInfo {
   private:
    BundleId _id;
    std::vector<TypeId> _component_ids;  // explicit components followed by required components, explicit order matches
                                         // bundle write order
    std::vector<RequiredComponentConstructor> _required_components;
    std::size_t _explicit_components_count;

    BundleInfo(BundleId id,
               std::vector<TypeId> component_ids,
               std::vector<RequiredComponentConstructor> required_components,
               std::size_t explicit_components_count)
        : _id(id),
          _component_ids(std::move(component_ids)),
          _required_components(std::move(required_components)),
          _explicit_components_count(explicit_components_count) {}

   public:
    static BundleInfo create(
        std::string_view bundle_type_name,
        Storage& storage,
        const Components& components,
        std::vector<TypeId> component_ids,  // should be the same order as bundle write and bundle type ids
        BundleId id);

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

    template <typename T1, typename T2, is_bundle T3>
    void write_components(
        Table& table,  // The table at row should be previously allocated, either existing or uninitialized
        SparseSets& sparse_sets,
        const TypeRegistry& type_registry,
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
                 std::ranges::view<std::decay_t<T1>> && std::same_as<std::ranges::range_value_t<T1>, ComponentStatus>
    {
        using BundleType = Bundle<std::decay_t<T3>>;
        // debug assert check whether bundle types match explicit component ids
        assert(std::ranges::all_of(std::views::zip(BundleType::type_ids(type_registry), explicit_components()),
                                   [](auto&& pair) {
                                       auto&& [bundle_id, explicit_id] = pair;
                                       return bundle_id == explicit_id;
                                   }) &&
               std::ranges::size(BundleType::type_ids(type_registry)) == std::ranges::size(explicit_components()));
        std::vector<void*> pointers;
        pointers.reserve(_explicit_components_count);
        for (auto&& [type_id, status] : std::views::zip(explicit_components(), component_statuses)) {
            auto storage_type = components.get(type_id).value().get().storage_type();
            if (storage_type == StorageType::Table) {
                Dense& dense = table.get_dense_mut(type_id).value().get();
                void* ptr    = dense.get_mut(row).value();  // resize uninitialized already called
                if (status == ComponentStatus::Added) {
                    pointers.push_back(ptr);
                    dense.get_added_tick(row).value().get()    = tick;
                    dense.get_modified_tick(row).value().get() = tick;
                } else if (replace_existing) {
                    // manually destroy existing component before replacing
                    dense.type_info().destruct(ptr);
                    pointers.push_back(ptr);
                    dense.get_modified_tick(row).value().get() = tick;
                } else {
                    // keep existing, do nothing
                    pointers.push_back(nullptr);
                }
            } else {
                ComponentSparseSet& sparse_set = sparse_sets.get_mut(type_id).value().get();
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
                    sparse_set.type_info().destruct(ptr);
                    pointers.push_back(ptr);
                    sparse_set.get_modified_tick(entity).value().get() = tick;
                } else {
                    // keep existing, do nothing
                    pointers.push_back(nullptr);
                }
            }
        }
        Bundle<std::decay_t<T3>>::write(std::forward<T3>(bundle), pointers);

        for (auto&& rc : required_components) {
            (*rc)(table, sparse_sets, tick, row, entity);
        }
    }

    ArchetypeId insert_bundle_into_archetype(Archetypes& archetypes,
                                             Storage& storage,
                                             const Components& components,
                                             ArchetypeId archetype_id) const noexcept;
    std::optional<ArchetypeId> remove_bundle_from_archetype(Archetypes& archetypes,
                                                            Storage& storage,
                                                            const Components& components,
                                                            ArchetypeId archetype_id,
                                                            bool ignore_missing) const noexcept;
};
struct Bundles {
   public:
    std::size_t size() const { return _bundle_infos.size(); }
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
    template <is_bundle T>
    BundleId register_info(const TypeRegistry& type_registry, Components& components, Storage& storage) {
        using type   = std::decay_t<T>;
        auto type_id = type_registry.type_id<type>();
        if (auto it = _bundle_ids.find(type_id); it != _bundle_ids.end()) {
            // already registered
            return it->second;
        }
        Bundle<type>::register_components(type_registry, components);
        BundleId new_id = static_cast<BundleId>(_bundle_infos.size());
        auto info =
            BundleInfo::create(meta::type_id<type>().name(), storage, components,
                               Bundle<type>::type_ids(type_registry) | std::ranges::to<std::vector<TypeId>>(), new_id);
        _bundle_infos.emplace_back(std::move(info));
        _bundle_ids.emplace(type_id, new_id);
        return new_id;
    }
    template <is_bundle T>
    BundleId register_contributed_info(const TypeRegistry& type_registry, Components& components, Storage& storage) {
        using type   = std::decay_t<T>;
        auto type_id = type_registry.type_id<type>();
        if (auto it = _contributed_bundle_ids.find(type_id); it != _contributed_bundle_ids.end()) {
            // already registered
            return it->second;
        }
        BundleId explicit_id = register_info<type>(type_registry, components, storage);
        BundleId dyn_id      = init_dynamic_info(
            storage, components, _bundle_infos[explicit_id].all_components() | std::ranges::to<std::vector>());
        _contributed_bundle_ids.emplace(type_id, dyn_id);
        return dyn_id;
    }

    BundleId init_dynamic_info(Storage& storage, const Components& components, std::vector<TypeId> ids);

    BundleId init_component_info(Storage& storage, const Components& components, TypeId type_id);

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

    std::unordered_map<std::vector<TypeId>, BundleId, VecHash> _dynamic_bundle_ids;
    std::unordered_map<BundleId, std::vector<StorageType>, std::hash<std::size_t>> _dynamic_bundle_storages;

    // Cache for optimizing single component bundles
    std::unordered_map<TypeId, BundleId> _dynamic_component_ids;
    std::unordered_map<BundleId, StorageType, std::hash<std::size_t>> _dynamic_component_storages;
};

const Bundles& world_bundles(const World& world);
Bundles& world_bundles_mut(World& world);
}  // namespace core
#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <epix/traits.hpp>
#include <epix/utils.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

#include <epix/ecs/archetype.hpp>
#include <epix/ecs/component.hpp>
#include <epix/ecs/storage.hpp>
#include <epix/ecs/type_registry.hpp>

namespace epix::ecs {
EPIX_EXPORT template <typename T>
struct Bundle {};

EPIX_EXPORT template <typename B>
concept is_bundle = requires(std::decay_t<B>& b) {
    {
        Bundle<std::decay_t<B>>::get_components(
            b, std::declval<utils::function_ref<void(utils::function_ref<void(void*)>)>>())
    } -> std::same_as<void>;
    { Bundle<std::decay_t<B>>::type_ids(std::declval<const TypeRegistry&>()) } -> internal::type_id_view;
    { Bundle<std::decay_t<B>>::register_components(std::declval<const TypeRegistry&>(), std::declval<Components&>()) };
};

EPIX_EXPORT enum class InsertMode {
    Replace,
    Keep,
};

namespace internal {
struct BundleRef {
   public:
    BundleRef(is_bundle auto& bundle) {
        using type     = std::decay_t<decltype(bundle)>;
        using bundle_t = Bundle<type>;
        ref            = static_cast<void*>(std::addressof(bundle));
        static VTable vt{
            .get_components =
                [](void* ref, utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) {
                    type* b = static_cast<type*>(ref);
                    bundle_t::get_components(*b, write_component);
                },
            .type_ids =
                [](const TypeRegistry& reg) { return std::ranges::to<std::vector<TypeId>>(bundle_t::type_ids(reg)); },
            .register_components = [](const TypeRegistry& reg,
                                      Components& comp) { bundle_t::register_components(reg, comp); }};
        vtable = &vt;
    }
    void get_components(utils::function_ref<void(utils::function_ref<void(void*)>)> write_component) {
        vtable->get_components(ref, write_component);
    }
    std::vector<TypeId> type_ids(const TypeRegistry& reg) { return vtable->type_ids(reg); }
    void register_components(const TypeRegistry& reg, Components& comp) { vtable->register_components(reg, comp); }

   private:
    struct VTable {
        void (*get_components)(void*, utils::function_ref<void(utils::function_ref<void(void*)>)>);
        std::vector<TypeId> (*type_ids)(const TypeRegistry&);  // when calling this function, we always need a
                                                               // vector, so this won't affect performance
        void (*register_components)(const TypeRegistry&, Components&);
    }* vtable;
    void* ref;
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

    BundleId id() const noexcept { return _id; }
    auto explicit_components() const noexcept { return std::views::take(_component_ids, _explicit_components_count); }
    auto required_components() const noexcept { return std::views::drop(_component_ids, _explicit_components_count); }
    auto all_components() const noexcept { return std::views::all(_component_ids); }
    auto required_component_constructors() const noexcept { return std::views::all(_required_components); }

    void write_components(
        Table& table,  // The table at row should be previously allocated, either existing or uninitialized
        SparseSets& sparse_sets,
        const TypeRegistry& type_registry,
        const Components& components,
        std::span<const ComponentStatus> component_statuses,  // status of each explicit component
        std::span<const RequiredComponentConstructor>
            required_components,  // the required component constructors for required components needed to be added
        Entity entity,
        TableRow row,
        Tick tick,
        BundleRef bundle,
        InsertMode insert_mode = InsertMode::Replace) const;

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
    std::size_t size() const noexcept { return _bundle_infos.size(); }
    bool empty() const noexcept { return _bundle_infos.empty(); }
    auto iter() const noexcept { return std::views::all(_bundle_infos); }
    std::optional<std::reference_wrapper<const BundleInfo>> get(BundleId id) const noexcept {
        if (id.get() >= _bundle_infos.size()) {
            return std::nullopt;
        }
        return std::cref(_bundle_infos[id.get()]);
    }
    const BundleInfo& unsafe_get(BundleId id) const noexcept {
        assert(id.get() < _bundle_infos.size());
        return _bundle_infos[id.get()];
    }
    std::optional<BundleId> get_id(TypeId type_id) const noexcept {
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
                               std::ranges::to<std::vector<TypeId>>(Bundle<type>::type_ids(type_registry)), new_id);
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
            storage, components, std::ranges::to<std::vector<TypeId>>(_bundle_infos[explicit_id].all_components()));
        _contributed_bundle_ids.emplace(type_id, dyn_id);
        return dyn_id;
    }

    BundleId init_dynamic_info(Storage& storage, const Components& components, std::vector<TypeId> ids);

    BundleId init_component_info(Storage& storage, const Components& components, TypeId type_id);

    // Get the storage type of a single-component dynamic bundle
    std::optional<StorageType> get_storage(BundleId id) const noexcept {
        if (auto it = _dynamic_component_storages.find(id); it != _dynamic_component_storages.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    // Get the storage types of a multi-component dynamic bundle
    std::optional<std::span<const StorageType>> get_storages(BundleId id) const noexcept {
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
}  // namespace internal
}  // namespace epix::ecs
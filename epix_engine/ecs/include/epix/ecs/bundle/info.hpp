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
#include <epix/ecs/type_id.hpp>

namespace epix::ecs {

EPIX_EXPORT template <typename T>
struct Bundle {};

EPIX_EXPORT template <typename B>
concept is_bundle = requires(std::decay_t<B>& b) {
    {
        Bundle<std::decay_t<B>>::get_components(
            b, std::declval<utils::function_ref<void(utils::function_ref<void(void*)>)>>())
    } -> std::same_as<void>;
    {
        Bundle<std::decay_t<B>>::type_ids(std::declval<const Components&>())
    } -> traits::view_of_value<std::optional<TypeId>>;
    {
        Bundle<std::decay_t<B>>::register_components(std::declval<ComponentsRegistrator&>())
    } -> traits::view_of_value<TypeId>;
};

EPIX_EXPORT enum class InsertMode { Replace, Keep };

namespace internal {
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

    void write_components(Table& table,
                          SparseSets& sparse_sets,
                          const Components& components,
                          traits::view_of_value<ComponentStatus> auto&& component_statuses,
                          traits::view_of_value<const RequiredComponentConstructor&> auto&& required_components,
                          Entity entity,
                          TableRow row,
                          Tick tick,
                          is_bundle auto&& bundle,
                          InsertMode insert_mode = InsertMode::Replace) const {
        auto component_id_status_view = std::views::zip(explicit_components(), component_statuses);
        auto component_iter           = component_id_status_view.begin();
        bundle.get_components([&](std::invocable<void*> auto&& write_component) {
            auto&& [type_id, status] = *component_iter;
            auto storage_type        = components.get_info(type_id)->get().storage_type();
            if (storage_type == StorageType::Table) {
                Dense& dense = table.unsafe_dense_mut(type_id);
                void* ptr    = dense.unsafe_get_mut(row);  // resize uninitialized already called
                if (status == ComponentStatus::Added) {
                    write_component(ptr);
                    dense.unsafe_added_tick_mut(row)    = tick;
                    dense.unsafe_modified_tick_mut(row) = tick;
                } else if (insert_mode == InsertMode::Replace) {
                    // manually destroy existing component before replacing
                    dense.type_info().destruct(ptr);
                    write_component(ptr);
                    dense.unsafe_modified_tick_mut(row) = tick;
                } else {
                    // keep existing, do nothing
                }
            } else {
                ComponentSparseSet& sparse_set = sparse_sets.unsafe_get_mut(type_id);
                assert(((status == ComponentStatus::Added) == !sparse_set.contains(entity)));
                if (status == ComponentStatus::Added || insert_mode == InsertMode::Replace) {
                    sparse_set.construct(entity, tick, [&](void* ptr) { write_component(ptr); });
                } else {
                    // keep existing, do nothing
                }
            }
            ++component_iter;
        });

        for (auto&& rc : required_components) {
            rc.initialize(table, sparse_sets, tick, row, entity);
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
    BundleId register_info(ComponentsRegistrator& components, Storage& storage) {
        using type   = std::decay_t<T>;
        auto type_id = components.register_component<type>();
        if (auto it = _bundle_ids.find(type_id); it != _bundle_ids.end()) {
            // already registered
            return it->second;
        }
        auto ids = std::ranges::to<std::vector>(Bundle<type>::register_components(components));
        BundleId new_id = static_cast<BundleId>(_bundle_infos.size());
        auto info =
            BundleInfo::create(meta::type_id<type>().name(), storage, components, ids, new_id);
        _bundle_infos.emplace_back(std::move(info));
        _bundle_ids.emplace(type_id, new_id);
        return new_id;
    }
    template <is_bundle T>
    BundleId register_contributed_info(ComponentsRegistrator& components, Storage& storage) {
        using type   = std::decay_t<T>;
        auto type_id = components.register_component<type>();
        if (auto it = _contributed_bundle_ids.find(type_id); it != _contributed_bundle_ids.end()) {
            // already registered
            return it->second;
        }
        BundleId explicit_id = register_info<type>(components, storage);
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

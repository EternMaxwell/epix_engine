#include <spdlog/spdlog.h>

#include <epix/ecs/bundle.hpp>
#include <epix/ecs/entities.hpp>

namespace epix::ecs {
internal::BundleInfo internal::BundleInfo::create(std::string_view bundle_type_name,
                                                  Storage& storage,
                                                  const Components& components,
                                                  std::vector<TypeId> component_ids,
                                                  BundleId id) {
    spdlog::trace("[bundle] Creating BundleInfo '{}' id={} with {} components.", bundle_type_name, id.get(),
                  component_ids.size());
    auto deduped = std::ranges::to<std::unordered_set<TypeId>>(component_ids);
    if (deduped.size() != std::ranges::size(component_ids)) {
        auto seen  = std::unordered_set<TypeId>{};
        auto duped = std::views::filter(component_ids, [&](TypeId tid) {
            if (seen.contains(tid)) {
                return true;
            } else {
                seen.insert(tid);
                return false;
            }
        });
        throw std::logic_error(std::format("bundle \"{}\" has duplicate component types {}", bundle_type_name,
                                           std::views::transform(duped, [&](TypeId tid) {
                                               return components.get(tid).value().get().type_index().name();
                                           })));
    }

    size_t explicit_count = std::ranges::size(component_ids);
    RequiredComponents required_components;
    for (auto&& id : deduped) {
        const ComponentInfo& info = components.get(id).value().get();
        required_components.merge(info.required_components());
        storage.prepare_component(info);
    }
    auto required_constructors = std::ranges::to<std::vector<RequiredComponentConstructor>>(
        std::views::transform(std::views::filter(required_components.components,
                                                 [&](auto&& v) {
                                                     auto&& [type_id, rc] = v;
                                                     return !deduped.contains(type_id);
                                                 }),
                              [&](auto&& v) {
                                  auto&& [type_id, rc] = v;
                                  storage.prepare_component(components.get(type_id).value().get());
                                  component_ids.push_back(type_id);
                                  return rc.constructor;
                              }));

    return BundleInfo(id, std::move(component_ids), required_constructors, explicit_count);
}

ArchetypeId internal::BundleInfo::insert_bundle_into_archetype(Archetypes& archetypes,
                                                               Storage& storage,
                                                               const Components& components,
                                                               ArchetypeId archetype_id) const noexcept {
    spdlog::trace("[bundle] Inserting bundle {} into archetype {}.", _id.get(), archetype_id.get());
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

    for (auto&& [index, type_id] : std::views::enumerate(required_components())) {
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
            table_components = std::ranges::to<std::vector>(archetype.table_components());
            std::sort(table_components.begin(), table_components.end());
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
            archetypes.get_id_or_insert(new_table_id, std::move(table_components), std::move(sparse_components)).first;

        auto& archetype = archetypes.get_mut(archetype_id).value().get();
        archetype.edges_mut().cache_archetype_after_bundle_insert(
            _id, new_archetype_id, component_status, added_required_components, added_components, existing_components);
        return new_archetype_id;
    }
}

std::optional<ArchetypeId> internal::BundleInfo::remove_bundle_from_archetype(Archetypes& archetypes,
                                                                              Storage& storage,
                                                                              const Components& components,
                                                                              ArchetypeId archetype_id,
                                                                              bool ignore_missing) const noexcept {
    spdlog::trace("[bundle] Removing bundle {} from archetype {} (ignore_missing={}).", _id.get(), archetype_id.get(),
                  ignore_missing);
    {
        auto& edges = archetypes.get_mut(archetype_id).value().get().edges();
        auto&& opt =
            ignore_missing ? edges.get_archetype_after_bundle_remove(_id) : edges.get_archetype_after_bundle_take(_id);
        if (opt) return *opt;
    }
    // Not cached.
    std::vector<TypeId> next_table_components;
    std::vector<TypeId> next_sparse_components;
    TableId next_table_id;
    {
        auto& archetype = archetypes.get_mut(archetype_id).value().get();
        std::unordered_set<TypeId> table_components_set =
            std::ranges::to<std::unordered_set<TypeId>>(archetype.table_components());
        std::unordered_set<TypeId> sparse_components_set =
            std::ranges::to<std::unordered_set<TypeId>>(archetype.sparse_components());
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
        next_table_components  = std::ranges::to<std::vector>(table_components_set);
        next_sparse_components = std::ranges::to<std::vector>(sparse_components_set);
        std::sort(next_table_components.begin(), next_table_components.end());
        std::sort(next_sparse_components.begin(), next_sparse_components.end());
        if (!table_changed) {
            next_table_id = archetype.table_id();
        } else {
            next_table_id = storage.tables.get_id_or_insert(next_table_components);
        }
    }
    ArchetypeId next_archetype_id =
        archetypes.get_id_or_insert(next_table_id, std::move(next_table_components), std::move(next_sparse_components))
            .first;
    auto& archetype = archetypes.get_mut(archetype_id).value().get();
    if (ignore_missing) {
        // remove
        archetype.edges_mut().cache_archetype_after_bundle_remove(_id, next_archetype_id);
    } else {
        // take
        archetype.edges_mut().cache_archetype_after_bundle_take(_id, next_archetype_id);
    }
    return next_archetype_id;
}

internal::BundleId internal::Bundles::init_dynamic_info(Storage& storage,
                                                        const Components& components,
                                                        std::vector<TypeId> ids) {
    if (auto it = _dynamic_bundle_ids.find(ids); it != _dynamic_bundle_ids.end()) {
        return it->second;
    } else {
        BundleId new_id                   = static_cast<BundleId>(_bundle_infos.size());
        std::vector<StorageType> storages = std::ranges::to<std::vector<StorageType>>(std::views::transform(
            ids, [&](TypeId type_id) { return components.get(type_id).value().get().storage_type(); }));
        BundleInfo info                   = BundleInfo::create("dynamic bundle", storage, components, ids, new_id);
        _bundle_infos.emplace_back(std::move(info));
        _dynamic_bundle_storages.emplace(new_id, std::move(storages));
        _dynamic_bundle_ids.emplace(std::move(ids), new_id);
        return new_id;
    }
}

internal::BundleId internal::Bundles::init_component_info(Storage& storage,
                                                          const Components& components,
                                                          TypeId type_id) {
    if (auto it = _dynamic_component_ids.find(type_id); it != _dynamic_component_ids.end()) {
        return it->second;
    } else {
        BundleId new_id = static_cast<BundleId>(_bundle_infos.size());
        BundleInfo info = BundleInfo::create("component bundle", storage, components, {type_id}, new_id);
        _bundle_infos.emplace_back(std::move(info));
        StorageType storage_type = components.get(type_id).value().get().storage_type();
        _dynamic_component_storages.emplace(new_id, storage_type);
        _dynamic_component_ids.emplace(type_id, new_id);
        return new_id;
    }
}

EntityLocation internal::BundleRemover::remove(Entity entity, EntityLocation location) {
    assert(location.archetype_id == archetype_->id());
    // Not templated on bundle type, since we don't need to write components
    auto& bundle_info    = *bundle_info_;
    auto& dest_archetype = *new_archetype_;
    auto& src_archetype  = *archetype_;

    // trigger on_remove for components in the bundle
    world_trigger_on_remove(*world_, src_archetype, entity, bundle_info.explicit_components());

    location = world_entities(*world_).get(entity).value();  // in case it may be changed by on_remove

    auto result = src_archetype.swap_remove(location.archetype_idx);
    if (result.swapped_entity) {
        // swapped entity should update its location
        auto swapped_entity            = result.swapped_entity.value();
        auto swapped_location          = world_entities(*world_).get(swapped_entity).value();
        swapped_location.archetype_idx = location.archetype_idx;
        world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
    }
    bool same_table     = (src_archetype.table_id() == dest_archetype.table_id());
    bool same_archetype = (src_archetype.id() == dest_archetype.id());
    if (!same_table) {
        auto& new_table  = world_storage_mut(*world_).tables.get_mut(dest_archetype.table_id()).value().get();
        auto move_result = table_->move_to(result.table_row, new_table);
        if (move_result.swapped_entity) {
            // swapped entity should update its location
            auto swapped_entity        = move_result.swapped_entity.value();
            auto swapped_location      = world_entities(*world_).get(swapped_entity).value();
            swapped_location.table_idx = result.table_row;
            world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
            auto& swapped_archetype =
                world_archetypes_mut(*world_).get_mut(swapped_location.archetype_id).value().get();
            swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
        }
        location = dest_archetype.allocate(entity, move_result.new_index);
    } else {
        location = dest_archetype.allocate(entity, result.table_row);
    }
    for (auto&& type_id : bundle_info.explicit_components()) {
        world_components(*world_).get(type_id).and_then([&](const ComponentInfo& info) -> std::optional<bool> {
            // Not registered component will be ignored
            auto storage_type = info.storage_type();
            if (storage_type == StorageType::SparseSet) {
                auto& sparse_set = world_storage_mut(*world_).sparse_sets.get_mut(type_id).value().get();
                if (sparse_set.contains(entity)) sparse_set.remove(entity);
            }
            return true;
        });
    }
    world_entities_mut(*world_).set(entity.index, location);
    return location;
}

}  // namespace epix::ecs

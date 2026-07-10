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

void internal::BundleInfo::write_components(
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
    InsertMode insert_mode) const {
    auto component_id_status_view = std::views::zip(explicit_components(), component_statuses);
    auto component_iter           = component_id_status_view.begin();
    bundle.get_components([&](std::invocable<void*> auto&& write_component) {
        auto&& [type_id, status] = *component_iter;
        auto storage_type        = components.unsafe_get(type_id).storage_type();
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
        (*rc)(table, sparse_sets, tick, row, entity);
    }
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

internal::BundleRemover internal::BundleRemover::create_with_id(World& world,
                                                                ArchetypeId archetype_id,
                                                                BundleId bundle_id,
                                                                Tick tick) {
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

internal::BundleRemover internal::BundleRemover::create_with_type_id(World& world,
                                                                     ArchetypeId archetype_id,
                                                                     TypeId type_id,
                                                                     Tick tick) {
    auto& bundles      = world_bundles_mut(world);
    BundleId bundle_id = bundles.init_component_info(world_storage_mut(world), world_components(world), type_id);
    return create_with_id(world, archetype_id, bundle_id, tick);
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

internal::BundleInserter internal::BundleInserter::create_with_id(World& world,
                                                                  ArchetypeId archetype_id,
                                                                  BundleId bundle_id,
                                                                  Tick tick) {
    auto& bundles      = world_bundles_mut(world);
    auto& components   = world_components_mut(world);
    auto& storage      = world_storage_mut(world);
    auto& archetypes   = world_archetypes_mut(world);
    auto&& bundle_info = bundles.unsafe_get(bundle_id);
    ArchetypeId new_archetype_id =
        bundle_info.insert_bundle_into_archetype(archetypes, storage, components, archetype_id);
    Archetype& archetype = archetypes.unsafe_get_mut(archetype_id);
    BundleInserter inserter;
    inserter.world_                  = &world;
    inserter.archetype_after_insert_ = &archetype.edges().unsafe_archetype_after_bundle_insert_detail(bundle_id);
    inserter.bundle_info_            = &bundle_info;
    inserter.archetype_              = &archetype;
    inserter.table_                  = &storage.tables.unsafe_get_mut(archetype.table_id());
    inserter.change_tick_            = tick;
    return inserter;
}

EntityLocation internal::BundleInserter::insert(Entity entity,
                                                EntityLocation location,
                                                BundleRef bundle,
                                                InsertMode insert_mode) const {
    assert(location.archetype_id == archetype_->id());
    auto& bundle_info         = *bundle_info_;
    auto& dest_archetype      = world_archetypes_mut(*world_).unsafe_get_mut(archetype_after_insert_->archetype_id);
    const bool same_archetype = (archetype_->id() == dest_archetype.id());
    const bool same_table     = (archetype_->table_id() == dest_archetype.table_id());
    const bool should_replace = insert_mode == InsertMode::Replace;

    // trigger on_replace if replacing existing components in the bundle
    if (should_replace) {
        world_trigger_on_replace(*world_, *archetype_, entity, archetype_after_insert_->existing());
        world_trigger_on_remove(*world_, *archetype_, entity, archetype_after_insert_->existing());
    }
    location = world_entities(*world_).unsafe_get(entity);  // in case it may be changed by on_replace

    location = [&] {
        if (same_archetype) {
            // same archetype, just write components in place
            bundle_info.write_components(*table_, world_storage_mut(*world_).sparse_sets, world_type_registry(*world_),
                                         world_components(*world_), archetype_after_insert_->iter_status(),
                                         archetype_after_insert_->required_components, entity, location.table_idx,
                                         change_tick_, bundle, insert_mode);
            // location not changed
            return location;
        } else if (same_table) {
            // table not changed, but archetype changed due to sparse components
            auto result = archetype_->swap_remove(location.archetype_idx);
            if (result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity            = result.swapped_entity.value();
                auto swapped_location          = world_entities(*world_).unsafe_get(swapped_entity);
                swapped_location.archetype_idx = location.archetype_idx;
                world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
            }
            auto new_location = dest_archetype.allocate(entity, result.table_row);
            world_entities_mut(*world_).set(entity.index, new_location);
            bundle_info.write_components(*table_, world_storage_mut(*world_).sparse_sets, world_type_registry(*world_),
                                         world_components(*world_), archetype_after_insert_->iter_status(),
                                         archetype_after_insert_->required_components, entity, result.table_row,
                                         change_tick_, bundle, insert_mode);
            return new_location;
        } else {
            auto& new_table = world_storage_mut(*world_).tables.unsafe_get_mut(dest_archetype.table_id());
            auto& table     = *table_;
            auto result     = archetype_->swap_remove(location.archetype_idx);
            if (result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity            = result.swapped_entity.value();
                auto swapped_location          = world_entities(*world_).unsafe_get(swapped_entity);
                swapped_location.archetype_idx = location.archetype_idx;
                world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
            }
            auto move_result  = table.move_to(result.table_row, new_table);
            auto new_location = dest_archetype.allocate(entity, move_result.new_index);
            world_entities_mut(*world_).set(entity.index, new_location);
            if (move_result.swapped_entity) {
                // swapped entity should update its location
                auto swapped_entity        = move_result.swapped_entity.value();
                auto swapped_location      = world_entities(*world_).unsafe_get(swapped_entity);
                swapped_location.table_idx = result.table_row;
                world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                auto& swapped_archetype = world_archetypes_mut(*world_).unsafe_get_mut(swapped_location.archetype_id);
                swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
            }
            bundle_info.write_components(new_table, world_storage_mut(*world_).sparse_sets,
                                         world_type_registry(*world_), world_components(*world_),
                                         archetype_after_insert_->iter_status(),
                                         archetype_after_insert_->required_components, entity, move_result.new_index,
                                         change_tick_, bundle, insert_mode);
            return new_location;
        }
    }();
    // trigger on_add for newly added components in the bundle
    world_trigger_on_add(*world_, dest_archetype, entity, archetype_after_insert_->added());
    // trigger on_insert for newly added components in the bundle and existing components if replaced
    if (should_replace) {
        world_trigger_on_insert(*world_, dest_archetype, entity, archetype_after_insert_->inserted());
    } else {
        world_trigger_on_insert(*world_, dest_archetype, entity, archetype_after_insert_->added());
    }
    location = world_entities(*world_).unsafe_get(entity);  // in case it may be changed by on_add or on_insert

    return location;
}

internal::BundleSpawner internal::BundleSpawner::create_with_id(World& world, BundleId bundle_id, Tick tick) {
    auto& bundles      = world_bundles_mut(world);
    auto& components   = world_components_mut(world);
    auto& storage      = world_storage_mut(world);
    auto& archetypes   = world_archetypes_mut(world);
    auto&& bundle_info = bundles.unsafe_get(bundle_id);
    ArchetypeId new_archetype_id =
        bundle_info.insert_bundle_into_archetype(archetypes, storage, components, ArchetypeId(0));
    Archetype& archetype = archetypes.unsafe_get_mut(new_archetype_id);
    BundleSpawner spawner;
    spawner.world_       = &world;
    spawner.bundle_info_ = &bundle_info;
    spawner.archetype_   = &archetype;
    spawner.table_       = &storage.tables.unsafe_get_mut(archetype.table_id());
    spawner.change_tick_ = tick;
    return spawner;
}

void internal::BundleSpawner::reserve_storage(std::size_t additional) {
    if (additional == 0) return;
    auto& table     = *table_;
    auto& archetype = *archetype_;
    table.reserve(table.size() + additional);
    archetype.reserve(archetype.size() + additional);
}
EntityLocation internal::BundleSpawner::spawn_non_exist(Entity entity, BundleRef bundle) {
    auto& bundle_info = *bundle_info_;
    auto& archetype   = *archetype_;
    auto& table       = *table_;
    TableRow row      = table.allocate(entity);
    auto location     = archetype.allocate(entity, row);
    world_entities_mut(*world_).set(entity.index, location);
    auto spawn_bundle_status = std::ranges::to<std::vector>(std::views::take(
        std::views::repeat(ComponentStatus::Added), std::ranges::size(bundle_info.explicit_components())));
    bundle_info.write_components(table, world_storage_mut(*world_).sparse_sets, world_type_registry(*world_),
                                 world_components(*world_), spawn_bundle_status,
                                 bundle_info.required_component_constructors(), entity, row, change_tick_, bundle,
                                 InsertMode::Replace);
    // trigger on_add for newly added components in the bundle
    world_trigger_on_add(*world_, archetype, entity, archetype.components());
    // trigger on_insert for newly added components in the bundle
    world_trigger_on_insert(*world_, archetype, entity, archetype.components());

    location = world_entities(*world_).unsafe_get(entity);  // in case it may be changed by on_add or on_insert
    return location;
}

}  // namespace epix::ecs

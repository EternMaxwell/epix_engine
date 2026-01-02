module;

#include <cassert>

export module epix.core:bundle.spec;

import std;

import :bundle.interface;
import :world.decl;

namespace core {
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
    requires((specialization_of<ArgTuples, std::tuple> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             ((constructible_from_tuple<Ts, ArgTuples>/*  ||
               (std::same_as<Ts, std::monostate> && (std::tuple_size_v<ArgTuples> == 1) &&
                is_bundle<std::tuple_element_t<0, ArgTuples>>) */) &&
              ...))
struct InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> {
    // stores the args for constructing each component in Ts
    using storage_type = std::tuple<ArgTuples...>;
    storage_type args;

    /**
     * @brief Construct bundle types in place at the provided pointers.
     * The stored argument values should have lifetimes that extend beyond this call.
     * @param pointers
     */
    std::size_t write(std::span<void*> pointers) {
        assert(std::ranges::size(pointers) >= sizeof...(Ts));
        std::span<void*> span = pointers;

        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    // if ATuple has only one element and that element is same as T after decay, and element is bundle,
                    // then write as a bundle
                    if constexpr (std::tuple_size_v<ATuple> == 1 &&
                                  (std::same_as<T, std::decay_t<std::tuple_element_t<0, ATuple>>> ||
                                   std::same_as<T, std::monostate>) &&
                                  is_bundle<std::tuple_element_t<0, ATuple>>) {
                        // write as a bundle
                        using BundleType = Bundle<std::decay_t<std::tuple_element_t<0, ATuple>>>;
                        // write to sub range of pointers since a sub bundle might be also a bundle of multiple
                        // components
                        auto inserted = BundleType::write(std::get<0>(std::get<I>(args)), span);
                        span          = span.subspan(inserted);
                    } else {
                        if (T* ptr = static_cast<T*>(span[0])) {
                            []<std::size_t... Js>(std::index_sequence<Js...>, T* p, ATuple& atuple) {
                                new (p) T(std::forward<std::tuple_element_t<Js, ATuple>>(
                                    std::get<Js>(atuple))...);  // construct T in place with args from atuple
                            }(std::make_index_sequence<std::tuple_size_v<ATuple>>{}, ptr, std::get<I>(args));
                        }
                        span = span.subspan(1);
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return span.data() - pointers.data();
    }

    static auto type_ids(const TypeRegistry& registry) {
        std::vector<TypeId> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    if constexpr (std::tuple_size_v<ATuple> == 1 &&
                                  std::same_as<T, std::decay_t<std::tuple_element_t<0, ATuple>>> &&
                                  is_bundle<std::tuple_element_t<0, ATuple>>) {
                        // bundle type
                        using BundleType = Bundle<std::decay_t<std::tuple_element_t<0, ATuple>>>;
                        ids.insert_range(ids.end(), BundleType::type_ids(registry));
                    } else {
                        ids.push_back(registry.type_id<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return std::move(ids);
    }
    static void register_components(const TypeRegistry& registry, Components& components) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T      = std::tuple_element_t<I, std::tuple<Ts...>>;
                    using ATuple = std::tuple_element_t<I, storage_type>;
                    if constexpr (std::tuple_size_v<ATuple> == 1 &&
                                  std::same_as<T, std::decay_t<std::tuple_element_t<0, ATuple>>> &&
                                  is_bundle<std::tuple_element_t<0, ATuple>>) {
                        // bundle type
                        using BundleType = Bundle<std::decay_t<std::tuple_element_t<0, ATuple>>>;
                        BundleType::register_components(registry, components);
                    } else {
                        components.register_info<T>();
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }
};
template <typename... Ts, typename... ArgTuples>
    requires((specialization_of<ArgTuples, std::tuple> && ...) && (sizeof...(Ts) == sizeof...(ArgTuples)) &&
             (constructible_from_tuple<Ts, ArgTuples> && ...)) &&
            (sizeof...(ArgTuples) > 0)
InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>> make_bundle(ArgTuples&&... args) {
    return InitializeBundle<std::tuple<Ts...>, std::tuple<ArgTuples...>>{
        std::make_tuple(std::forward<ArgTuples>(args)...)};
}
template <typename... Ts>
    requires(std::constructible_from<std::decay_t<Ts>, Ts> && ...)
InitializeBundle<std::tuple<std::decay_t<Ts>...>, std::tuple<std::tuple<Ts>...>> make_bundle(Ts&&... args) {
    return InitializeBundle<std::tuple<std::decay_t<Ts>...>, std::tuple<std::tuple<Ts>...>>{
        std::make_tuple(std::tuple<Ts>(std::forward<Ts>(args))...)};
}
template <typename T>
    requires(specialization_of<T, InitializeBundle>)
struct Bundle<T> {
    static std::size_t write(T& bundle, std::span<void*> pointers) { return bundle.write(pointers); }
    static auto type_ids(const TypeRegistry& registry) { return T::type_ids(registry); }
    static void register_components(const TypeRegistry& registry, Components& components) {
        T::register_components(registry, components);
    }
};

template <typename... Ts>
struct RemoveBundle {
    std::size_t write(std::span<void*> pointers) {
        // A bundle used for remove only.
        return 0;
    }
    static auto type_ids(const TypeRegistry& registry) {
        std::vector<TypeId> ids;
        ids.reserve(sizeof...(Ts));
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
                    if constexpr (is_bundle<T>) {
                        using BundleType = Bundle<T>;
                        ids.insert_range(ids.end(), BundleType::type_ids(registry));
                    } else {
                        ids.push_back(registry.type_id<T>());
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
        return std::move(ids);
    }
    static void register_components(const TypeRegistry& registry, Components& components) {
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
                    using T = std::tuple_element_t<I, std::tuple<Ts...>>;
                    if constexpr (is_bundle<T>) {
                        using BundleType = Bundle<T>;
                        BundleType::register_components(registry, components);
                    } else {
                        components.register_info<T>();
                    }
                }(std::integral_constant<std::size_t, Is>{}),
                ...);
        }(std::make_index_sequence<sizeof...(Ts)>());
    }
};
template <typename T>
    requires(specialization_of<T, RemoveBundle>)
struct Bundle<T> {
    static std::size_t write(T& bundle, std::span<void*> pointers) { return bundle.write(pointers); }
    static auto type_ids(const TypeRegistry& registry) { return T::type_ids(registry); }
    static void register_components(const TypeRegistry& registry, Components& components) {
        T::register_components(registry, components);
    }
};

struct BundleInserter {
   public:
    static BundleInserter create_with_id(World& world, ArchetypeId archetype_id, BundleId bundle_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        auto& components   = world_components_mut(world);
        auto& storage      = world_storage_mut(world);
        auto& archetypes   = world_archetypes_mut(world);
        auto&& bundle_info = bundles.get(bundle_id).value().get();
        ArchetypeId new_archetype_id =
            bundle_info.insert_bundle_into_archetype(archetypes, storage, components, archetype_id);
        Archetype& archetype = archetypes.get_mut(archetype_id).value().get();
        BundleInserter inserter;
        inserter.world_ = &world;
        inserter.archetype_after_insert_ =
            &archetype.edges().get_archetype_after_bundle_insert_detail(bundle_id).value().get();
        inserter.bundle_info_ = &bundle_info;
        inserter.archetype_   = &archetype;
        inserter.table_       = &storage.tables.get_mut(archetype.table_id()).value().get();
        inserter.change_tick_ = tick;
        return inserter;
    }
    template <is_bundle T>
    static BundleInserter create(World& world, ArchetypeId archetype_id, Tick tick) {
        auto& components = world_components(world);
        auto& bundles    = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, archetype_id, bundle_id, tick);
    }

    EntityLocation insert(Entity entity,
                          EntityLocation location,
                          is_bundle auto&& bundle,
                          bool replace_existing) const {
        assert(location.archetype_id == archetype_->id());
        auto& bundle_info = *bundle_info_;
        auto& dest_archetype =
            world_archetypes_mut(*world_).get_mut(archetype_after_insert_->archetype_id).value().get();
        const bool same_archetype = (archetype_->id() == dest_archetype.id());
        const bool same_table     = (archetype_->table_id() == dest_archetype.table_id());

        // trigger on_replace if replacing existing components in the bundle
        if (replace_existing) {
            world_trigger_on_replace(*world_, *archetype_, entity, archetype_after_insert_->existing());
            world_trigger_on_remove(*world_, *archetype_, entity, archetype_after_insert_->existing());
        }
        location = world_entities(*world_).get(entity).value();  // in case it may be changed by on_replace

        location = [&] {
            if (same_archetype) {
                // same archetype, just write components in place
                bundle_info.write_components(*table_, world_storage_mut(*world_).sparse_sets,
                                             world_type_registry(*world_), world_components(*world_),
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
                    auto swapped_location          = world_entities(*world_).get(swapped_entity).value();
                    swapped_location.archetype_idx = location.archetype_idx;
                    world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                }
                auto new_location = dest_archetype.allocate(entity, result.table_row);
                world_entities_mut(*world_).set(entity.index, new_location);
                bundle_info.write_components(*table_, world_storage_mut(*world_).sparse_sets,
                                             world_type_registry(*world_), world_components(*world_),
                                             archetype_after_insert_->iter_status(),
                                             archetype_after_insert_->required_components | std::views::all, entity,
                                             result.table_row, change_tick_, bundle, replace_existing);
                return new_location;
            } else {
                auto& new_table = world_storage_mut(*world_).tables.get_mut(dest_archetype.table_id()).value().get();
                auto& table     = *table_;
                auto result     = archetype_->swap_remove(location.archetype_idx);
                if (result.swapped_entity) {
                    // swapped entity should update its location
                    auto swapped_entity            = result.swapped_entity.value();
                    auto swapped_location          = world_entities(*world_).get(swapped_entity).value();
                    swapped_location.archetype_idx = location.archetype_idx;
                    world_entities_mut(*world_).set(swapped_entity.index, swapped_location);
                }
                auto move_result  = table.move_to(result.table_row, new_table);
                auto new_location = dest_archetype.allocate(entity, move_result.new_index);
                world_entities_mut(*world_).set(entity.index, new_location);
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
                bundle_info.write_components(new_table, world_storage_mut(*world_).sparse_sets,
                                             world_type_registry(*world_), world_components(*world_),
                                             archetype_after_insert_->iter_status(),
                                             archetype_after_insert_->required_components | std::views::all, entity,
                                             move_result.new_index, change_tick_, bundle, replace_existing);
                return new_location;
            }
        }();
        // trigger on_add for newly added components in the bundle
        world_trigger_on_add(*world_, dest_archetype, entity, archetype_after_insert_->added());
        // trigger on_insert for newly added components in the bundle and existing components if replaced
        if (replace_existing) {
            world_trigger_on_insert(*world_, dest_archetype, entity, archetype_after_insert_->inserted());
        } else {
            world_trigger_on_insert(*world_, dest_archetype, entity, archetype_after_insert_->added());
        }
        location = world_entities(*world_).get(entity).value();  // in case it may be changed by on_add or on_insert

        return location;
    }

   private:
    World* world_ = nullptr;
    const ArchetypeAfterBundleInsert* archetype_after_insert_;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Table* table_;
    Tick change_tick_;
};

// Bundle spawner for insert entities newly spawned (that is, not actually added to any archetype yet, otherwise will
// adding a 'move from empty archetype to target archetype' operation)
struct BundleSpawner {
   public:
    static BundleSpawner create_with_id(World& world, BundleId bundle_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        auto& components   = world_components_mut(world);
        auto& storage      = world_storage_mut(world);
        auto& archetypes   = world_archetypes_mut(world);
        auto&& bundle_info = bundles.get(bundle_id).value().get();
        ArchetypeId new_archetype_id =
            bundle_info.insert_bundle_into_archetype(archetypes, storage, components, ArchetypeId(0));
        Archetype& archetype = archetypes.get_mut(new_archetype_id).value().get();
        BundleSpawner spawner;
        spawner.world_       = &world;
        spawner.bundle_info_ = &bundle_info;
        spawner.archetype_   = &archetype;
        spawner.table_       = &storage.tables.get_mut(archetype.table_id()).value().get();
        spawner.change_tick_ = tick;
        return spawner;
    }
    template <is_bundle T>
    static BundleSpawner create(World& world, Tick tick) {
        auto& bundles = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, bundle_id, tick);
    }

    void reserve_storage(std::size_t additional) {
        if (additional == 0) return;
        auto& table     = *table_;
        auto& archetype = *archetype_;
        table.reserve(table.size() + additional);
        archetype.reserve(archetype.size() + additional);
    }
    template <typename T>
    EntityLocation spawn_non_exist(Entity entity, T&& bundle)
        requires is_bundle<std::decay_t<T>>
    {
        auto& bundle_info = *bundle_info_;
        auto& archetype   = *archetype_;
        auto& table       = *table_;
        TableRow row      = table.allocate(entity);
        auto location     = archetype.allocate(entity, row);
        world_entities_mut(*world_).set(entity.index, location);
        auto spawn_bundle_status = std::views::repeat(ComponentStatus::Added) |
                                   std::views::take(std::ranges::size(bundle_info.explicit_components()));
        bundle_info.write_components(table, world_storage_mut(*world_).sparse_sets, world_type_registry(*world_),
                                     world_components(*world_), spawn_bundle_status,
                                     bundle_info.required_component_constructors(), entity, row, change_tick_, bundle,
                                     true);
        // trigger on_add for newly added components in the bundle
        world_trigger_on_add(*world_, archetype, entity, archetype.components());
        // trigger on_insert for newly added components in the bundle
        world_trigger_on_insert(*world_, archetype, entity, archetype.components());

        location = world_entities(*world_).get(entity).value();  // in case it may be changed by on_add or on_insert
        return location;
    }

   private:
    World* world_ = nullptr;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Table* table_;
    Tick change_tick_;
};
struct BundleRemover {
   public:
    static BundleRemover create_with_id(World& world, ArchetypeId archetype_id, BundleId bundle_id, Tick tick) {
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
    template <is_bundle T>
    static BundleRemover create(World& world, ArchetypeId archetype_id, Tick tick) {
        auto& bundles = world_bundles_mut(world);
        BundleId bundle_id =
            bundles.register_info<T>(world_type_registry(world), world_components_mut(world), world_storage_mut(world));
        return create_with_id(world, archetype_id, bundle_id, tick);
    }
    static BundleRemover create_with_type_id(World& world, ArchetypeId archetype_id, TypeId type_id, Tick tick) {
        auto& bundles      = world_bundles_mut(world);
        BundleId bundle_id = bundles.init_component_info(world_storage_mut(world), world_components(world), type_id);
        return create_with_id(world, archetype_id, bundle_id, tick);
    }

    EntityLocation remove(Entity entity, EntityLocation location) {
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

   private:
    World* world_ = nullptr;
    const BundleInfo* bundle_info_;
    Archetype* archetype_;
    Archetype* new_archetype_;
    Table* table_;
    Tick change_tick_;
};
}  // namespace core
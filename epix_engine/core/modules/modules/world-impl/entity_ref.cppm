module;

#include <cassert>

export module epix.core:world.entity_ref;

import std;

import :world.entity_ref.decl;
import :entities;
import :ticks;
import :world.decl;
import :bundle.spec;
import :storage;
import :hierarchy;

namespace core {
/**
 * @brief Read only reference to an entity in a World. Can get mutable components, but cannot do structural changes.
 */
export struct EntityRef {
   protected:
    Entity entity_;
    EntityLocation location_;
    const World* world_;

    friend struct World;

   public:
    EntityRef(Entity entity, const World* world)
        : entity_(entity),
          world_(world),
          location_(world_entities(*world).get(entity).value_or(EntityLocation::invalid())) {}
    void update_location() { location_ = world_entities(*world_).get(entity_).value_or(EntityLocation::invalid()); }
    void assert_not_despawned() const {
        assert(location_.archetype_id != EntityLocation::invalid().archetype_id && "Entity has been despawned");
    }
    Entity id() const { return entity_; }
    EntityLocation location() const { return location_; }
    const Archetype& archetype() const { return world_archetypes(*world_).get(location_.archetype_id).value().get(); }
    bool contains_id(TypeId component_id) const { return archetype().contains(component_id); }
    template <typename T>
    bool contains() const {
        return contains_id(world_type_registry(*world_).type_id<T>());
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get() const {
        TypeId type_id = world_type_registry(*world_).type_id<T>();
        return world_components(*world_).get(type_id).and_then(
            [&](const ComponentInfo& info) -> std::optional<std::reference_wrapper<const T>> {
                auto storage_type = info.storage_type();
                if (storage_type == StorageType::Table) {
                    return world_storage(*world_).tables.get(location_.table_id).and_then([&](const Table& table) {
                        return table.get_dense(type_id).and_then(
                            [&](const Dense& dense) { return dense.get_as<T>(location_.table_idx); });
                    });
                } else {
                    return world_storage(*world_).sparse_sets.get(type_id).and_then(
                        [&](const ComponentSparseSet& cs) { return cs.get_as<T>(entity_); });
                }
            });
    }
    template <typename T>
    std::optional<Ref<T>> get_ref() const {
        TypeId type_id = world_type_registry(*world_).type_id<T>();
        return world_components(*world_).get(type_id).and_then([&](const ComponentInfo& info) {
            auto storage_type = info.storage_type();
            if (storage_type == StorageType::Table) {
                return world_storage(*world_).tables.get(location_.table_id).and_then([&](const Table& table) {
                    return table.get_dense(type_id).and_then([&](const Dense& dense) {
                        return dense.get_as<T>(location_.table_idx).transform([&](const T& value) {
                            return Ref<T>(
                                &value, Ticks::from_refs(dense.get_tick_refs(location_.table_idx).value(),
                                                         world_last_change_tick(*world_), world_change_tick(*world_)));
                        });
                    });
                });
            } else {
                return world_storage(*world_).sparse_sets.get(type_id).and_then([&](const ComponentSparseSet& cs) {
                    return cs.get_as<T>(entity_).transform([&](const T& value) {
                        return Ref<T>(
                            &value, Ticks::from_refs(cs.get_tick_refs(entity_).value(), world_last_change_tick(*world_),
                                                     world_change_tick(*world_)));
                    });
                });
            }
        });
    }
    std::optional<ComponentTicks> get_ticks_by_id(TypeId type_id) const {
        return world_components(*world_).get(type_id).and_then(
            [&](const ComponentInfo& info) -> std::optional<ComponentTicks> {
                auto storage_type = info.storage_type();
                if (storage_type == StorageType::Table) {
                    return world_storage(*world_).tables.get(location_.table_id).and_then([&](const Table& table) {
                        return table.get_dense(type_id).and_then(
                            [&](const Dense& dense) { return dense.get_ticks(location_.table_idx); });
                    });
                } else {
                    return world_storage(*world_).sparse_sets.get(type_id).and_then(
                        [&](const ComponentSparseSet& cs) { return cs.get_ticks(entity_); });
                }
            });
    }
    template <typename T>
    std::optional<ComponentTicks> get_ticks() const {
        return get_ticks_by_id(world_type_registry(*world_).type_id<T>());
    }
};
export struct EntityRefMut : public EntityRef {
   protected:
    friend struct World;
    World* world_;

   public:
    EntityRefMut(Entity entity, World* world) : EntityRef(entity, world), world_(world) {}
    template <typename T>
    std::optional<Mut<T>> get_mut() {
        TypeId type_id = world_type_registry(*world_).type_id<T>();
        return world_components(*world_).get(type_id).and_then([&](const ComponentInfo& info) {
            auto storage_type = info.storage_type();
            if (storage_type == StorageType::Table) {
                return world_storage_mut(*world_).tables.get_mut(location_.table_id).and_then([&](Table& table) {
                    return table.get_dense_mut(type_id).and_then([&](Dense& dense) {
                        return dense.get_as_mut<T>(location_.table_idx).transform([&](T& value) {
                            return Mut<T>(&value, TicksMut::from_refs(dense.get_tick_refs(location_.table_idx).value(),
                                                                      world_last_change_tick(*world_),
                                                                      world_change_tick(*world_)));
                        });
                    });
                });
            } else {
                return world_storage_mut(*world_).sparse_sets.get_mut(type_id).and_then([&](ComponentSparseSet& cs) {
                    return cs.get_as_mut<T>(entity_).transform([&](T& value) {
                        return Mut<T>(&value,
                                      TicksMut::from_refs(cs.get_tick_refs(entity_).value(),
                                                          world_last_change_tick(*world_), world_change_tick(*world_)));
                    });
                });
            }
        });
    }
};

export struct EntityWorldMut : public EntityRefMut {
   public:
    using EntityRefMut::EntityRefMut;

    template <typename T>
    void insert_internal(T&& bundle, bool replace_existing)
        requires(is_bundle<std::decay_t<T>>)
    {
        assert_not_despawned();
        auto inserter =
            BundleInserter::create<std::decay_t<T>>(*world_, location_.archetype_id, world_change_tick(*world_));
        location_ = inserter.insert(entity_, location_, std::forward<T>(bundle), replace_existing);
        world_flush(*world_);
        update_location();
    }
    template <typename... Ts, typename... Args>
    void emplace(Args&&... args)
        requires(sizeof...(Args) == sizeof...(Ts))
    {
        insert_internal(make_bundle<Ts...>(std::forward<Args>(args)...), true);
    }
    template <typename... Ts, typename... Args>
    void emplace_if_new(Args&&... args)
        requires(sizeof...(Args) == sizeof...(Ts))
    {
        insert_internal(make_bundle<Ts...>(std::forward<Args>(args)...), false);
    }
    template <typename... Ts>
    void insert(Ts&&... components) {
        insert_internal(make_bundle<std::decay_t<Ts>...>(std::forward_as_tuple(std::forward<Ts>(components))...), true);
    }
    template <typename... Ts>
    void insert_if_new(Ts&&... components) {
        insert_internal(make_bundle<std::decay_t<Ts>...>(std::forward_as_tuple(std::forward<Ts>(components))...),
                        false);
    }
    template <typename B>
    void insert_bundle(B&& bundle)
        requires(is_bundle<std::decay_t<B>>)
    {
        insert_internal(std::forward<B>(bundle), true);
    }
    template <typename B>
    void insert_bundle_if_new(B&& bundle)
        requires(is_bundle<std::decay_t<B>>)
    {
        insert_internal(std::forward<B>(bundle), false);
    }
    template <typename... Ts>
    void remove() {
        BundleId id = world_bundles_mut(*world_).register_info<RemoveBundle<Ts...>>(
            world_type_registry(*world_), world_components_mut(*world_), world_storage_mut(*world_));
        remove_bundle(id);
    }
    void remove_bundle(BundleId bundle_id);
    bool remove_by_id(TypeId type_id);
    void remove_by_ids(type_id_view auto&& type_ids) {
        try {
            auto bundle_id =
                world_bundles_mut(*world_).init_dynamic_info(world_storage_mut(*world_), world_components(*world_),
                                                             type_ids | std::ranges::to<std::vector<TypeId>>());
            remove_bundle(bundle_id);
        } catch (const std::bad_optional_access&) {
            return;  // some component not found, do nothing
        }
    }
    void clear();
    void despawn();
    /// Spawn a new entity as a child of this entity and return a mutable reference to it
    template <typename... Args>
    EntityWorldMut spawn(Args&&... args)
        requires((std::constructible_from<std::decay_t<Args>, Args> || is_bundle<Args>) && ...)
    {
        auto spawn_bundle = [&]<typename T>(T&& bundle) {
            world_flush(*world_);  // needed for Entities::alloc.
            auto e       = world_entities_mut(*world_).alloc();
            auto spawner = BundleSpawner::create<T&&>(*world_, world_change_tick(*world_));
            spawner.spawn_non_exist(e, std::forward<T>(bundle));
            world_flush(*world_);  // flush to ensure no delayed operations.
            return EntityWorldMut(e, world_);
        };
        auto mut = [&] {
            if constexpr (sizeof...(Args) == 1 && (is_bundle<Args> && ...)) {
                return spawn_bundle(std::forward<Args>(args)...);
            }
            return spawn_bundle(make_bundle<std::decay_t<Args>...>(std::forward_as_tuple(std::forward<Args>(args))...));
        }();
        mut.insert(Parent{entity_});
        update_location();
        return mut;
    }
    EntityWorldMut& then(std::invocable<EntityWorldMut&> auto&& func) {
        func(*this);
        update_location();
        return *this;
    }
};
}  // namespace core
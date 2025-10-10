#pragma once

#include "../archetype.hpp"
#include "../bundle.hpp"
#include "../bundleimpl.hpp"
#include "../change_detection.hpp"
#include "../component.hpp"
#include "../entities.hpp"
#include "../storage.hpp"
#include "../world_cell.hpp"

namespace epix::core {
/**
 * @brief Read only reference to an entity in a World. Can get mutable components, but cannot do structural changes.
 */
struct EntityRef {
   protected:
    Entity entity_;
    EntityLocation location_;
    WorldCell* world_;

    friend struct World;

   public:
    EntityRef(Entity entity, WorldCell* world)
        : entity_(entity),
          world_(world),
          location_(world->entities().get(entity).value_or(EntityLocation::invalid())) {}
    void update_location() { location_ = world_->entities().get(entity_).value_or(EntityLocation::invalid()); }
    void assert_not_despawned() const {
        assert(location_.archetype_id != EntityLocation::invalid().archetype_id && "Entity has been despawned");
    }
    Entity id() const { return entity_; }
    EntityLocation location() const { return location_; }
    const Archetype& archetype() const { return world_->archetypes().get(location_.archetype_id).value().get(); }
    bool contains_id(TypeId component_id) const { return archetype().contains(component_id); }
    template <typename T>
    bool contains() const {
        return contains_id(world_->type_registry().type_id<T>());
    }
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get() const {
        TypeId type_id = world_->type_registry().type_id<T>();
        return world_->components().get(type_id).and_then(
            [&](const ComponentInfo& info) -> std::optional<std::reference_wrapper<const T>> {
                auto storage_type = info.storage_type();
                if (storage_type == StorageType::Table) {
                    return world_->storage().tables.get(location_.table_id).and_then([&](const storage::Table& table) {
                        return table.get_dense(type_id).and_then(
                            [&](const storage::Dense& dense) { return dense.get_as<T>(location_.table_idx); });
                    });
                } else {
                    return world_->storage().sparse_sets.get(type_id).and_then(
                        [&](const storage::ComponentSparseSet& cs) { return cs.get_as<T>(entity_); });
                }
            });
    }
    template <typename T>
    std::optional<Ref<T>> get_ref() const {
        TypeId type_id = world_->type_registry().type_id<T>();
        return world_->components().get(type_id).and_then([&](const ComponentInfo& info) {
            auto storage_type = info.storage_type();
            if (storage_type == StorageType::Table) {
                return world_->storage().tables.get(location_.table_id).and_then([&](const storage::Table& table) {
                    return table.get_dense(type_id).and_then([&](const storage::Dense& dense) {
                        return dense.get_as<T>(location_.table_idx).transform([&](const T& value) {
                            return Ref<T>(&value, Ticks::from_refs(dense.get_tick_refs(location_.table_idx).value(),
                                                                   world_->last_change_tick(), world_->change_tick()));
                        });
                    });
                });
            } else {
                return world_->storage().sparse_sets.get(type_id).and_then([&](const storage::ComponentSparseSet& cs) {
                    return cs.get_as<T>(entity_).transform([&](const T& value) {
                        return Ref<T>(&value, Ticks::from_refs(cs.get_tick_refs(entity_).value(),
                                                               world_->last_change_tick(), world_->change_tick()));
                    });
                });
            }
        });
    }
    std::optional<ComponentTicks> get_ticks_by_id(TypeId type_id) const {
        return world_->components().get(type_id).and_then(
            [&](const ComponentInfo& info) -> std::optional<ComponentTicks> {
                auto storage_type = info.storage_type();
                if (storage_type == StorageType::Table) {
                    return world_->storage().tables.get(location_.table_id).and_then([&](const storage::Table& table) {
                        return table.get_dense(type_id).and_then(
                            [&](const storage::Dense& dense) { return dense.get_ticks(location_.table_idx); });
                    });
                } else {
                    return world_->storage().sparse_sets.get(type_id).and_then(
                        [&](const storage::ComponentSparseSet& cs) { return cs.get_ticks(entity_); });
                }
            });
    }
    template <typename T>
    std::optional<ComponentTicks> get_ticks() const {
        return get_ticks_by_id(world_->type_registry().type_id<T>());
    }
};
struct EntityRefMut : public EntityRef {
   protected:
    friend struct World;

    template <typename T>
    void insert_internal(T&& bundle, bool replace_existing)
        requires(bundle::is_bundle<std::decay_t<T>>)
    {
        assert_not_despawned();
        auto inserter = BundleInserter::create<std::decay_t<T>>(*world_, location_.archetype_id, world_->change_tick());
        location_     = inserter.insert(entity_, location_, std::forward<T>(bundle), replace_existing);
        world_->flush();
        update_location();
    }

   public:
    using EntityRef::EntityRef;
    template <typename T>
    std::optional<Mut<T>> get_mut() {
        TypeId type_id = world_->type_registry().type_id<T>();
        return world_->components().get(type_id).and_then([&](const ComponentInfo& info) {
            auto storage_type = info.storage_type();
            if (storage_type == StorageType::Table) {
                return world_->storage_mut().tables.get_mut(location_.table_id).and_then([&](storage::Table& table) {
                    return table.get_dense_mut(type_id).and_then([&](storage::Dense& dense) {
                        return dense.get_as_mut<T>(location_.table_idx).transform([&](T& value) {
                            return Mut<T>(&value,
                                          TicksMut::from_refs(dense.get_tick_refs(location_.table_idx).value(),
                                                              world_->last_change_tick(), world_->change_tick()));
                        });
                    });
                });
            } else {
                return world_->storage_mut().sparse_sets.get_mut(type_id).and_then(
                    [&](storage::ComponentSparseSet& cs) {
                        return cs.get_as_mut<T>(entity_).transform([&](T& value) {
                            return Mut<T>(&value,
                                          TicksMut::from_refs(cs.get_tick_refs(entity_).value(),
                                                              world_->last_change_tick(), world_->change_tick()));
                        });
                    });
            }
        });
    }

    template <typename... Ts, typename... Args>
    void emplace(Args&&... args)
        requires(sizeof...(Args) == sizeof...(Ts))
    {
        // replace existing components
        insert_internal(make_init_bundle<Ts...>(std::forward<Args>(args)...), true);
    }
    template <typename... Ts, typename... Args>
    void emplace_if_new(Args&&... args)
        requires(sizeof...(Args) == sizeof...(Ts))
    {
        // do not replace existing components
        insert_internal(make_init_bundle<Ts...>(std::forward<Args>(args)...), false);
    }
    template <typename... Ts>
    void insert(Ts&&... components) {
        // replace existing components
        insert_internal(make_init_bundle<std::decay_t<Ts>...>(std::forward<Ts>(components)...), true);
    }
    template <typename... Ts>
    void insert_if_new(Ts&&... components) {
        // do not replace existing components
        insert_internal(make_init_bundle<std::decay_t<Ts>...>(std::forward<Ts>(components)...), false);
    }
    template <typename B>
    void insert_bundle(B&& bundle)
        requires(bundle::is_bundle<std::decay_t<B>>)
    {
        // replace existing components
        insert_internal(std::forward<B>(bundle), true);
    }
    template <typename B>
    void insert_bundle_if_new(B&& bundle)
        requires(bundle::is_bundle<std::decay_t<B>>)
    {
        // do not replace existing components
        insert_internal(std::forward<B>(bundle), false);
    }
    template <typename... Ts>
    void remove() {
        assert_not_despawned();
        auto remover =
            BundleRemover::create<RemoveBundle<Ts...>>(*world_, location_.archetype_id, world_->change_tick());
        location_ = remover.remove(entity_, location_);
        // world_->flush();
        // update_location();
    }
};
}  // namespace epix::core
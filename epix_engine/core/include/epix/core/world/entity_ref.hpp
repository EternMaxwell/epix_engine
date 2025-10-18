#pragma once

#include <optional>
#include <tuple>

#include "../archetype.hpp"
#include "../bundle.hpp"
#include "../bundleimpl.hpp"
#include "../change_detection.hpp"
#include "../component.hpp"
#include "../entities.hpp"
#include "../storage.hpp"
#include "../world.hpp"

namespace epix::core {
/**
 * @brief Read only reference to an entity in a World. Can get mutable components, but cannot do structural changes.
 */
struct EntityRef {
   protected:
    Entity entity_;
    EntityLocation location_;
    const World* world_;

    friend struct World;

   public:
    EntityRef(Entity entity, const World* world)
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
    World* world_;

   public:
    EntityRefMut(Entity entity, World* world) : EntityRef(entity, world), world_(world) {}
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
};

struct EntityWorldMut : public EntityRefMut {
   public:
    using EntityRefMut::EntityRefMut;

    // 结构性变更方法
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
    template <typename... Ts, typename... Args>
    void emplace(Args&&... args)
        requires(sizeof...(Args) == sizeof...(Ts))
    {
        insert_internal(make_init_bundle<Ts...>(std::forward<Args>(args)...), true);
    }
    template <typename... Ts, typename... Args>
    void emplace_if_new(Args&&... args)
        requires(sizeof...(Args) == sizeof...(Ts))
    {
        insert_internal(make_init_bundle<Ts...>(std::forward<Args>(args)...), false);
    }
    template <typename... Ts>
    void insert(Ts&&... components) {
        insert_internal(make_init_bundle<std::decay_t<Ts>...>(std::forward_as_tuple(std::forward<Ts>(components))...),
                        true);
    }
    template <typename... Ts>
    void insert_if_new(Ts&&... components) {
        insert_internal(make_init_bundle<std::decay_t<Ts>...>(std::forward_as_tuple(std::forward<Ts>(components))...),
                        false);
    }
    template <typename B>
    void insert_bundle(B&& bundle)
        requires(bundle::is_bundle<std::decay_t<B>>)
    {
        insert_internal(std::forward<B>(bundle), true);
    }
    template <typename B>
    void insert_bundle_if_new(B&& bundle)
        requires(bundle::is_bundle<std::decay_t<B>>)
    {
        insert_internal(std::forward<B>(bundle), false);
    }
    template <typename... Ts>
    void remove() {
        BundleId id = world_->bundles_mut().register_info<RemoveBundle<Ts...>>(
            world_->type_registry(), world_->components_mut(), world_->storage_mut());
        remove_bundle(id);
    }
    void remove_bundle(BundleId bundle_id) {
        assert_not_despawned();
        auto remover = BundleRemover::create_with_id(*world_, location_.archetype_id, bundle_id, world_->change_tick());
        location_    = remover.remove(entity_, location_);
        world_->flush();
        update_location();
    }
    bool remove_by_id(TypeId type_id) {
        if (!archetype().contains(type_id)) return false;
        auto bundle_id =
            world_->bundles_mut().init_component_info(world_->storage_mut(), world_->components(), type_id);
        remove_bundle(bundle_id);
        return true;
    }
    void remove_by_ids(bundle::type_id_view auto&& type_ids) {
        try {
            auto bundle_id =
                world_->bundles_mut().init_dynamic_info(world_->storage_mut(), world_->components(), type_ids);
            remove_bundle(bundle_id);
        } catch (const std::bad_optional_access&) {
            return;  // some component not found, do nothing
        }
    }
    void despawn() {
        assert_not_despawned();
        auto& entities  = world_->entities_mut();
        auto& archetype = world_->archetypes_mut().get_mut(location_.archetype_id).value().get();
        auto& table     = world_->storage_mut().tables.get_mut(archetype.table_id()).value().get();
        world_->trigger_on_remove(archetype, entity_, archetype.components());
        world_->trigger_on_despawn(archetype, entity_, archetype.components());
        location_ = world_->entities().get(entity_).value();
        world_->entities_mut().free(entity_);
        world_->flush_entities();
        auto result = archetype.swap_remove(location_.archetype_idx);
        if (result.swapped_entity) {
            auto swapped_entity            = result.swapped_entity.value();
            auto swapped_location          = entities.get(swapped_entity).value();
            swapped_location.archetype_idx = location_.archetype_idx;
            entities.set(swapped_entity.index, swapped_location);
        }
        auto table_result = table.swap_remove(result.table_row);
        if (table_result) {
            auto swapped_entity        = table_result.value();
            auto swapped_location      = entities.get(swapped_entity).value();
            swapped_location.table_idx = result.table_row;
            entities.set(swapped_entity.index, swapped_location);
            auto& swapped_archetype = world_->archetypes_mut().get_mut(swapped_location.archetype_id).value().get();
            swapped_archetype.set_entity_table_row(swapped_location.archetype_idx, swapped_location.table_idx);
        }
        for (auto&& type_id : archetype.sparse_components()) {
            world_->storage_mut().sparse_sets.get_mut(type_id).and_then([&](storage::ComponentSparseSet& cs) {
                cs.remove(entity_);
                return std::optional<bool>(true);
            });
        }
        world_->flush();
    }
};
}  // namespace epix::core

namespace epix::core {
// impl for World::spawn
template <typename... Ts, typename... Args>
EntityWorldMut World::spawn(Args&&... args)
    requires(sizeof...(Args) == sizeof...(Ts))
{
    if constexpr (sizeof...(Ts) == 0) {
        auto e = _entities.reserve_entity();  // reserving, no flush needed.
        flush();
        return EntityWorldMut(e, this);
    }
    return spawn(make_init_bundle<Ts...>(std::forward<Args>(args)...));
}
template <typename T>
EntityWorldMut World::spawn(T&& bundle)
    requires(bundle::is_bundle<std::remove_cvref_t<T>>)
{
    flush();  // needed for Entities::alloc.
    auto e       = _entities.alloc();
    auto spawner = BundleSpawner::create<std::remove_cvref_t<T>>(*this, change_tick());
    spawner.spawn_non_exist(e, std::forward<T>(bundle));
    flush();  // flush to ensure no delayed operations.
    return EntityWorldMut(e, this);
}

// impl for World::entity and entity_mut, get_entity and get_entity_mut
inline std::optional<EntityRef> World::get_entity(Entity entity) {
    if (auto loc = _entities.get(entity); loc.has_value() && loc.value() != EntityLocation::invalid()) {
        return EntityRef(entity, this);
    }
    return std::nullopt;
}
inline std::optional<EntityWorldMut> World::get_entity_mut(Entity entity) {
    if (auto loc = _entities.get(entity); loc.has_value() && loc.value() != EntityLocation::invalid()) {
        return EntityWorldMut(entity, this);
    }
    return std::nullopt;
}
inline EntityRef World::entity(Entity entity) { return get_entity(entity).value(); }
inline EntityWorldMut World::entity_mut(Entity entity) { return get_entity_mut(entity).value(); }
}  // namespace epix::core
/**
 * @file epix.core-world.cppm
 * @brief World partition for the main ECS world
 */

export module epix.core:world;

import :fwd;
import :entities;
import :type_system;
import :component;
import :archetype;
import :storage;
import :bundle;
import :query;
import :tick;

#include <atomic>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

export namespace epix::core {
    EPIX_MAKE_U64_WRAPPER(WorldId)
    
    // Command queue for deferred operations
    struct CommandQueue {
        void apply(World& world);
    };
    
    // Entity references
    struct EntityRef {
        const World* world;
        Entity entity;
    };
    
    struct EntityRefMut {
        World* world;
        Entity entity;
    };
    
    struct EntityWorldMut {
        World* world;
        Entity entity;
    };
    
    // From world trait
    template <typename T>
    struct FromWorld;
    
    template <typename T>
    concept is_from_world = requires(World& world) {
        { FromWorld<T>::emplace(std::declval<T&>(), world) };
    };
    
    // Main World structure
    struct World {
       private:
        WorldId _id;
        std::shared_ptr<type_system::TypeRegistry> _type_registry;
        Components _components;
        Entities _entities;
        Storage _storage;
        Archetypes _archetypes;
        Bundles _bundles;
        CommandQueue _command_queue;
        std::unique_ptr<std::atomic<uint32_t>> _change_tick;
        Tick _last_change_tick;
        
       public:
        World(WorldId id, std::shared_ptr<type_system::TypeRegistry> type_registry = std::make_shared<type_system::TypeRegistry>());
        
        WorldId id() const { return _id; }
        const type_system::TypeRegistry& type_registry() const { return *_type_registry; }
        const Components& components() const { return _components; }
        Components& components_mut() { return _components; }
        const Entities& entities() const { return _entities; }
        Entities& entities_mut() { return _entities; }
        const Storage& storage() const { return _storage; }
        Storage& storage_mut() { return _storage; }
        const Archetypes& archetypes() const { return _archetypes; }
        Archetypes& archetypes_mut() { return _archetypes; }
        const Bundles& bundles() const { return _bundles; }
        Bundles& bundles_mut() { return _bundles; }
        Tick change_tick() const { return _change_tick->load(std::memory_order_relaxed); }
        Tick increment_change_tick() { return Tick(_change_tick->fetch_add(1, std::memory_order_relaxed)); }
        Tick last_change_tick() const { return _last_change_tick; }
        CommandQueue& command_queue() { return _command_queue; }
        
        template <typename... Args>
        EntityWorldMut spawn(Args&&... args);
        
        template <typename T, typename... Args>
        void emplace_resource(Args&&... args);
        
        template <typename T>
        void insert_resource(T&& value);
        
        template <typename T>
        void init_resource() requires is_from_world<T>;
        
        bool remove_resource(TypeId type_id);
        
        template <typename T>
        bool remove_resource();
        
        template <typename T>
        std::optional<T> take_resource() requires std::movable<T>;
        
        template <typename T>
        std::optional<std::reference_wrapper<const T>> get_resource() const;
        
        template <typename T>
        std::optional<std::reference_wrapper<T>> get_resource_mut();
        
        template <typename T>
        const T& resource() const;
        
        template <typename T>
        T& resource_mut();
        
        template <typename D>
        query::QueryState<D, query::Filter<>> query() requires(query::valid_query_data<query::QueryData<D>>);
        
        template <typename D, typename F>
        query::QueryState<D, F> query_filtered() requires(query::valid_query_data<query::QueryData<D>> && query::valid_query_filter<query::QueryFilter<F>>);
        
        EntityRef entity(Entity entity);
        EntityWorldMut entity_mut(Entity entity);
        std::optional<EntityRef> get_entity(Entity entity);
        std::optional<EntityWorldMut> get_entity_mut(Entity entity);
        
        void flush_entities();
        void flush_commands();
        void flush();
        void clear_entities();
        void clear_resources();
    };
    
    // Deferred world for limited access
    struct DeferredWorld {
       private:
        World* world_;
        
       public:
        DeferredWorld(World& world) : world_(&world) {}
        
        WorldId id() const { return world_->id(); }
        const type_system::TypeRegistry& type_registry() const { return world_->type_registry(); }
        const Components& components() const { return world_->components(); }
        const Entities& entities() const { return world_->entities(); }
        const Storage& storage() const { return world_->storage(); }
        const Archetypes& archetypes() const { return world_->archetypes(); }
        const Bundles& bundles() const { return world_->bundles(); }
        Tick change_tick() const { return world_->change_tick(); }
        Tick last_change_tick() const { return world_->last_change_tick(); }
        CommandQueue& command_queue() { return world_->command_queue(); }
        
        EntityRef entity(Entity entity);
        EntityRefMut entity_mut(Entity entity);
        std::optional<EntityRef> get_entity(Entity entity);
        std::optional<EntityRefMut> get_entity_mut(Entity entity);
        
        template <typename T>
        std::optional<std::reference_wrapper<const T>> get_resource() const;
        
        template <typename T>
        std::optional<std::reference_wrapper<T>> get_resource_mut();
        
        template <typename T>
        const T& resource() const;
        
        template <typename T>
        T& resource_mut();
        
        template <typename D>
        query::QueryState<D, query::Filter<>> query() requires(query::valid_query_data<query::QueryData<D>>);
        
        template <typename D, typename F>
        query::QueryState<D, F> query_filtered() requires(query::valid_query_data<query::QueryData<D>> && query::valid_query_filter<query::QueryFilter<F>>);
    };
}  // namespace epix::core

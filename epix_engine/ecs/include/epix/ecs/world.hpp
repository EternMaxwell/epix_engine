#pragma once

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#endif

#include <epix/ecs/bundle.hpp>
#include <epix/ecs/component.hpp>
#include <epix/ecs/core/type_id.hpp>
#include <epix/ecs/entity/entities.hpp>
#include <epix/ecs/query.hpp>
#include <epix/ecs/storage.hpp>
#include <epix/ecs/world/commands.hpp>
#include <epix/ecs/world/detail/access.hpp>
#include <epix/ecs/world/entity_ref.hpp>
#include <epix/ecs/world/from_world.hpp>

namespace epix::ecs {

/** @brief Central ECS world that owns all entities, components, resources, and archetypes.
 *  Non-copyable and movable. Provides methods for spawning entities, managing resources,
 *  and querying components. Supports change detection via ticks. */
EPIX_EXPORT struct World {
   public:
    World(WorldId id)
        : _id(id),
          _components(),
          _storage(),
          _change_tick(std::make_unique<std::atomic<std::uint32_t>>(1)),
          _last_change_tick(0) {}
    World(const World&)            = delete;
    World(World&&)                 = default;
    World& operator=(const World&) = delete;
    World& operator=(World&&)      = default;

    /** @brief Get the world's unique identifier. */
    WorldId id() const noexcept { return _id; }
    ComponentsRegistrator registrator() { return ComponentsRegistrator(_components, _component_ids); }
    ComponentsQueuedRegistrator queued_registrator() const {
        return ComponentsQueuedRegistrator(_components, _component_ids);
    }
    /** @brief Get a const reference to the component metadata store. */
    const Components& components() const noexcept { return _components; }
    /** @brief Get a mutable reference to the component metadata store. */
    Components& components_mut() noexcept { return _components; }

    /** Register a default-constructed component R as required by T. */
    template <typename T, typename R>
        requires std::default_initializable<R>
    void register_required_components() {
        auto result = try_register_required_components<T, R>();
        if (!result) throw std::logic_error(result.error().message(_components));
    }

    /** Register R as required by T, using the supplied constructor. */
    template <typename T, typename F>
        requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
    void register_required_components_with(F&& constructor) {
        auto result = try_register_required_components_with<T>(std::forward<F>(constructor));
        if (!result) throw std::logic_error(result.error().message(_components));
    }

    /** Fallible form of register_required_components. */
    template <typename T, typename R>
        requires std::default_initializable<R>
    std::expected<void, RequiredComponentsError> try_register_required_components() {
        return try_register_required_components_with<T>([] { return R{}; });
    }

    /**
     * Fallible runtime registration for a required component.
     *
     * The requiree must not have appeared in an archetype yet, because bundle and
     * archetype edges cache required-component metadata.
     */
    template <typename T, typename F>
        requires std::invocable<F> && std::is_object_v<std::invoke_result_t<F>>
    std::expected<void, RequiredComponentsError> try_register_required_components_with(F&& constructor) {
        using R                    = std::invoke_result_t<F>;
        auto component_registrator = registrator();
        TypeId requiree            = component_registrator.template register_component<T>();
        if (_archetypes.by_component.contains(requiree)) {
            return std::unexpected(
                RequiredComponentsError{RequiredComponentsErrorKind::ArchetypeExists, requiree, requiree});
        }
        TypeId required = component_registrator.template register_component<R>();
        return _components.template register_required_components<R>(requiree, required, std::forward<F>(constructor));
    }
    /** @brief Get a const reference to the entity allocator. */
    const Entities& entities() const noexcept { return _entities; }
    /** @brief Get a mutable reference to the entity allocator. */
    Entities& entities_mut() noexcept { return _entities; }
    /** @brief Get a const reference to the storage (tables, sparse sets, resources). */
    const Storage& storage() const noexcept { return _storage; }
    /** @brief Get a mutable reference to the storage. */
    Storage& storage_mut() noexcept { return _storage; }
    /** @brief Get a const reference to all archetypes. */
    const Archetypes& archetypes() const noexcept { return _archetypes; }
    /** @brief Get a mutable reference to all archetypes. */
    Archetypes& archetypes_mut() noexcept { return _archetypes; }
    /** @brief Get a const reference to the bundle registry. */
    const internal::Bundles& bundles() const noexcept { return _bundles; }
    /** @brief Get a mutable reference to the bundle registry. */
    internal::Bundles& bundles_mut() noexcept { return _bundles; }
    /** @brief Get the current change tick (monotonically increasing). */
    Tick change_tick() const noexcept { return _change_tick->load(std::memory_order_relaxed); }
    /** @brief Atomically increment and return the previous change tick. */
    Tick increment_change_tick() noexcept { return Tick(_change_tick->fetch_add(1, std::memory_order_relaxed)); }
    /** @brief Get the tick value from the last time change ticks were checked. */
    Tick last_change_tick() const noexcept { return _last_change_tick; }
    /** @brief Check and clamp stale change ticks in tables, sparse sets, and resources.
     *  @param additional_checks Extra tick-checking logic invoked with the current change tick. */
    void check_change_tick(std::invocable<Tick> auto&& additional_checks) {
        auto change_tick = this->change_tick();
        if (change_tick.relative_to(_last_change_tick).get() < ::epix::ecs::internal::CHECK_TICK_THRESHOLD) {
            return;
        }
        storage_mut().tables.check_change_ticks(change_tick);
        storage_mut().sparse_sets.check_change_ticks(change_tick);
        storage_mut().resources.check_change_ticks(change_tick);
        additional_checks(change_tick);
        _last_change_tick = change_tick;
    }
    /** @brief Get a mutable reference to the deferred command queue. */
    internal::CommandQueue& command_queue() noexcept { return _command_queue; }

    /** @brief Despawn all entities and clear archetype/table/sparse-set data. */
    void clear_entities() {
        _entities.clear();
        _archetypes.clear_entities();
        _storage.tables.clear();
        _storage.sparse_sets.clear_entities();
    }
    /** @brief Remove all resources from storage. */
    void clear_resources() { _storage.resources.clear(); }

    /** @brief Spawn a new entity with the given components or bundle.
     *  @tparam Args Component types or a single bundle type.
     *  @param args Component values to attach to the new entity.
     *  @return An EntityWorldMut handle for the newly created entity.
     *  @note Calls flush() internally, so all pending commands are applied. */
    template <typename... Args>
    EntityWorldMut spawn(Args&&... args)
        requires((std::constructible_from<std::decay_t<Args>, Args> || is_bundle<Args>) && ...)
    {
        auto spawn_bundle = [&]<typename T>(T&& bundle) {
            flush();  // needed for Entities::alloc.
            auto e       = _entities.alloc();
            auto spawner = internal::BundleSpawner::create<T&&>(*this, change_tick());
            spawner.spawn_non_exist(e, bundle);
            flush();  // flush to ensure no delayed operations.
            return EntityWorldMut(e, this);
        };
        if constexpr (sizeof...(Args) == 1 && (is_bundle<Args> && ...)) {
            return spawn_bundle(std::forward<Args>(args)...);
        }
        return spawn_bundle(make_bundle<std::decay_t<Args>...>(std::forward_as_tuple(std::forward<Args>(args))...));
    }

    /** @brief Construct and insert a resource of type T in-place.
     *  @tparam T Resource type.
     *  @tparam Args Constructor argument types.
     *  @param args Arguments forwarded to T's constructor. */
    template <typename T, typename... Args>
        requires std::constructible_from<T, Args&&...>
    void emplace_resource(Args&&... args) {
        auto id = registrator().register_resource<T>();
        _storage.resources.initialize(id, _components);
        _storage.resources.get_mut(id).value().get().template emplace<T>(change_tick(), std::forward<Args>(args)...);
    }
    /** @brief Insert a resource by moving or copying the given value.
     *  @tparam T Resource type (deduced). */
    template <typename T>
        requires std::constructible_from<std::remove_cvref_t<T>, T>
    void insert_resource(T&& value) {
        using D = std::remove_cvref_t<T>;
        emplace_resource<D>(std::forward<T>(value));
    }
    /** @brief Initialize a resource via FromWorld (default-construct, construct from World, or static from_world).
     *  @tparam T Resource type satisfying is_from_world.
     *  @note If construction throws, the resource slot is cleaned up and an error is logged. */
    template <typename T>
    TypeId init_resource()
        requires internal::is_from_world<T>
    {
        auto id = registrator().register_resource<T>();
        _storage.resources.initialize(id, _components);
        _storage.resources.get_mut(id).value().get().construct(change_tick(), [this, id](void* dest) {
            try {
                internal::FromWorld<T>::emplace(dest, *this);
            } catch (const std::exception& e) {
                spdlog::error("[app] Failed to initialize resource of type {}: {}", meta::type_id<T>::short_name(),
                              e.what());
                _storage.resources.get_mut(id).value().get().remove();
            } catch (...) {
                spdlog::error("[app] Failed to initialize resource of type {}: unknown error",
                              meta::type_id<T>::short_name());
                _storage.resources.get_mut(id).value().get().remove();
            }
        });
        return id;
    }
    /** @brief Remove a resource by its TypeId. Returns true if removed. */
    bool remove_resource(TypeId type_id) {
        return _storage.resources.get_mut(type_id)
            .and_then([](ResourceData& res) {
                res.remove();
                return std::optional<bool>(true);
            })
            .value_or(false);
    }
    /** @brief Remove a resource by type. Returns true if removed.
     *  @tparam T Resource type. */
    template <typename T>
    bool remove_resource() {
        return _components.get_valid_id<T>().and_then(std::bind_front(&World::remove_resource, this)).value_or(false);
    }
    /** @brief Remove and return a resource by type, if it exists.
     *  @tparam T Movable resource type.
     *  @return The moved resource, or std::nullopt if not present. */
    template <typename T>
    std::optional<T> take_resource()
        requires std::movable<T>
    {
        return _components.get_valid_id<T>()
            .and_then(std::bind_front(&Resources::get_mut, std::ref(_storage.resources)))
            .and_then(&ResourceData::take<T>);
    }
    /** @brief Get a const reference to a resource, if it exists.
     *  @tparam T Resource type.
     *  @return Optional const reference wrapper. */
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_resource() const {
        return _components.get_valid_id<T>()
            .and_then(std::bind_front(&Resources::get, std::ref(_storage.resources)))
            .and_then(&ResourceData::get_as<T>);
    }
    /** @brief Get a mutable reference to a resource, if it exists.
     *  @tparam T Resource type.
     *  @return Optional mutable reference wrapper. */
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_resource_mut() {
        return _components.get_valid_id<T>()
            .and_then(std::bind_front(&Resources::get_mut, std::ref(_storage.resources)))
            .and_then(&ResourceData::get_as_mut<T>);
    }
    /** @brief Get a const reference to a resource. Throws if not present.
     *  @tparam T Resource type. */
    template <typename T>
    const T& resource() const {
        return get_resource<T>().value().get();
    }
    /** @brief Get a mutable reference to a resource. Throws if not present.
     *  @tparam T Resource type. */
    template <typename T>
    T& resource_mut() {
        return get_resource_mut<T>().value().get();
    }

    /** @brief Get or initialize a resource. If the resource does not exist, it is
     *  created via FromWorld. @tparam T Resource type satisfying is_from_world. */
    template <internal::is_from_world T>
    T& resource_or_init() {
        return get_resource_mut<T>()
            .or_else([&]() -> std::optional<std::reference_wrapper<T>> {
                init_resource<T>();
                return std::ref(resource_mut<T>());
            })
            .value();
    }
    /** @brief Get or emplace a resource with the given constructor arguments.
     *  @tparam T Resource type.
     *  @tparam Args Constructor argument types. */
    template <typename T, typename... Args>
        requires std::constructible_from<T, Args&&...>
    T& resource_or_emplace(Args&&... args) {
        return get_resource_mut<T>()
            .or_else([&]() -> std::optional<std::reference_wrapper<T>> {
                emplace_resource<T>(std::forward<Args>(args)...);
                return std::ref(resource_mut<T>());
            })
            .value();
    }

    /**
     * @brief Invoke a function with resources from the world and return its result. The function's parameters must
     * be resources in the world, or World& itself. If the resource does not exist, and if it satisfies
     * is_from_world, it will be initialized. Otherwise, this functions will return std::unexpected.
     *
     * @tparam F The function type.
     * @param func The function to invoke.
     */
    template <typename F>
        requires requires {
            typename traits::function_traits<F>::args_tuple;
            typename traits::function_traits<F>::return_type;
        }
    std::expected<typename traits::function_traits<F>::return_type, std::monostate> resource_scope(F&& func) {
        using return_t                = typename traits::function_traits<F>::return_type;
        using args_tuple              = typename traits::function_traits<F>::args_tuple;
        auto try_get_or_init_resource = [this]<typename T>() -> std::optional<std::reference_wrapper<T>> {
            if constexpr (std::same_as<World, T>) {
                return std::ref(*this);
            }
            auto res = this->get_resource_mut<T>();
            if (!res) {
                if constexpr (internal::is_from_world<T>) {
                    res.emplace(std::ref(this->resource_or_init<T>()));
                }
            }
            return res;
        };
        auto get_param = [&]<std::size_t... I>(std::index_sequence<I...>)
            -> std::expected<std::tuple<std::tuple_element_t<I, args_tuple>...>, std::monostate> {
            // for each argument, try to get or init the resource, if any call to try_get_or_init_resource fails,
            // return std::nullopt
            std::tuple<
                std::optional<std::reference_wrapper<std::remove_reference_t<std::tuple_element_t<I, args_tuple>>>>...>
                params = {try_get_or_init_resource
                              .template operator()<std::remove_reference_t<std::tuple_element_t<I, args_tuple>>>()...};
            if (!(... && std::get<I>(params).has_value())) {
                return std::unexpected(std::monostate{});
            }
            return std::tuple<std::tuple_element_t<I, args_tuple>...>{(std::get<I>(params).value().get())...};
        };
        return get_param(std::make_index_sequence<std::tuple_size_v<args_tuple>>())
            .and_then([&](auto&& param_tuple) -> std::expected<return_t, std::monostate> {
                if constexpr (std::is_void_v<return_t>) {
                    std::apply(func, std::move(param_tuple));
                    return {};
                } else {
                    return std::apply(func, std::move(param_tuple));
                }
            });
    }

    void trigger_on_add(const Archetype& archetype, Entity entity, internal::type_id_view auto&& targets) {
        for (auto&& target : targets) {
            _components.get_info(target).and_then([&](const internal::ComponentInfo& info) -> std::optional<bool> {
                if (info.hooks().on_add) {
                    info.hooks().on_add(*this, HookContext{.entity = entity, .component_id = target});
                }
                return true;
            });
        }
    }
    void trigger_on_insert(const Archetype& archetype, Entity entity, internal::type_id_view auto&& targets) {
        for (auto&& target : targets) {
            _components.get_info(target).and_then([&](const internal::ComponentInfo& info) -> std::optional<bool> {
                if (info.hooks().on_insert) {
                    info.hooks().on_insert(*this, HookContext{.entity = entity, .component_id = target});
                }
                return true;
            });
        }
    }
    void trigger_on_replace(const Archetype& archetype, Entity entity, internal::type_id_view auto&& targets) {
        for (auto&& target : targets) {
            _components.get_info(target).and_then([&](const internal::ComponentInfo& info) -> std::optional<bool> {
                if (info.hooks().on_replace) {
                    info.hooks().on_replace(*this, HookContext{.entity = entity, .component_id = target});
                }
                return true;
            });
        }
    }
    void trigger_on_remove(const Archetype& archetype, Entity entity, internal::type_id_view auto&& targets) {
        for (auto&& target : targets) {
            _components.get_info(target).and_then([&](const internal::ComponentInfo& info) -> std::optional<bool> {
                if (info.hooks().on_remove) {
                    info.hooks().on_remove(*this, HookContext{.entity = entity, .component_id = target});
                }
                return true;
            });
        }
    }
    void trigger_on_despawn(const Archetype& archetype, Entity entity, internal::type_id_view auto&& targets) {
        for (auto&& target : targets) {
            _components.get_info(target).and_then([&](const internal::ComponentInfo& info) -> std::optional<bool> {
                if (info.hooks().on_despawn) {
                    info.hooks().on_despawn(*this, HookContext{.entity = entity, .component_id = target});
                }
                return true;
            });
        }
    }

    /** @brief Create a query over entities matching the given query data.
     *  @tparam D Query data descriptor (component references, etc.). */
    template <query_data D>
    QueryState<D, Filter<>> query() {
        return QueryState<D, Filter<>>::create(*this);
    }
    /** @brief Create a filtered query over entities.
     *  @tparam D Query data descriptor.
     *  @tparam F Query filter. */
    template <query_data D, query_filter F>
    QueryState<D, F> query_filtered() {
        return QueryState<D, F>::create(*this);
    }
    /** @brief Try to create a query from a const world. Returns std::nullopt if requirements cannot be met.
     *  @tparam D Query data descriptor. */
    template <query_data D>
    std::optional<QueryState<D, Filter<>>> try_query() const {
        return QueryState<D, Filter<>>::create_from_const(*this);
    }
    /** @brief Try to create a filtered query from a const world.
     *  @tparam D Query data descriptor.
     *  @tparam F Query filter. */
    template <query_data D, query_filter F>
    std::optional<QueryState<D, F>> try_query_filtered() const {
        return QueryState<D, F>::create_from_const(*this);
    }

    /** @brief Get a read-only reference to a living entity. Panics if the entity is invalid. */
    EntityRef entity(Entity entity) const { return get_entity(entity).value(); }
    /** @brief Get a mutable reference to a living entity. Panics if the entity is invalid. */
    EntityWorldMut entity_mut(Entity entity) { return get_entity_mut(entity).value(); }
    /** @brief Try to get a read-only reference to an entity. Returns std::nullopt if invalid. */
    std::optional<EntityRef> get_entity(Entity entity) const noexcept;
    /** @brief Try to get a mutable reference to an entity. Returns std::nullopt if invalid. */
    std::optional<EntityWorldMut> get_entity_mut(Entity entity) noexcept;

    /** @brief Flush pending reserved entities into the empty archetype. */
    void flush_entities();
    /** @brief Apply all deferred commands in the command queue. */
    void flush_commands();
    /** @brief Flush entities and apply deferred commands. */
    void flush();

   protected:
    WorldId _id;
    Components _components;
    ComponentIds _component_ids;
    Entities _entities;
    Storage _storage;
    Archetypes _archetypes;
    internal::Bundles _bundles;
    internal::CommandQueue _command_queue;
    std::unique_ptr<std::atomic<std::uint32_t>> _change_tick;
    Tick _last_change_tick;
};
/** @brief A deferred view of a World that provides read-only data access
 *  and deferred command submission. Does not allow direct mutation. */
struct DeferredWorld {
   public:
    DeferredWorld(World& world) noexcept : world_(&world) {}
    /** @brief Get the world's unique identifier. */
    WorldId id() const noexcept { return world_->id(); }
    ComponentsQueuedRegistrator queued_registrator() const noexcept { return world_->queued_registrator(); }
    /** @brief Get a const reference to the component metadata store. */
    const Components& components() const noexcept { return world_->components(); }
    /** @brief Get a const reference to the entity allocator. */
    const Entities& entities() const noexcept { return world_->entities(); }
    /** @brief Get a const reference to the storage. */
    const Storage& storage() const noexcept { return world_->storage(); }
    /** @brief Get a const reference to all archetypes. */
    const Archetypes& archetypes() const noexcept { return world_->archetypes(); }
    /** @brief Get a const reference to the bundle registry. */
    const internal::Bundles& bundles() const noexcept { return world_->bundles(); }
    /** @brief Get the current change tick. */
    Tick change_tick() const noexcept { return world_->change_tick(); }
    /** @brief Get the last change tick. */
    Tick last_change_tick() const noexcept { return world_->last_change_tick(); }
    /** @brief Get a mutable reference to the deferred command queue. */
    internal::CommandQueue& command_queue() noexcept { return world_->command_queue(); }

    /** @brief Get a read-only reference to a living entity. */
    EntityRef entity(Entity entity) const { return world_->entity(entity); }
    /** @brief Get a deferred mutable reference to a living entity. */
    EntityRefMut entity_mut(Entity entity) { return world_->entity_mut(entity); }
    /** @brief Try to get a read-only entity reference. */
    std::optional<EntityRef> get_entity(Entity entity) const { return world_->get_entity(entity); }
    /** @brief Try to get a deferred mutable entity reference. */
    std::optional<EntityRefMut> get_entity_mut(Entity entity) { return world_->get_entity_mut(entity); }

    /** @brief Get a const reference to a resource, if it exists. */
    template <typename T>
    std::optional<std::reference_wrapper<const T>> get_resource() const {
        return world_->get_resource<T>();
    }
    /** @brief Get a mutable reference to a resource, if it exists. */
    template <typename T>
    std::optional<std::reference_wrapper<T>> get_resource_mut() {
        return world_->get_resource_mut<T>();
    }
    /** @brief Get a const reference to a resource. Throws if not present. */
    template <typename T>
    const T& resource() const {
        return world_->resource<T>();
    }
    /** @brief Get a mutable reference to a resource. Throws if not present. */
    template <typename T>
    T& resource_mut() {
        return world_->resource_mut<T>();
    }
    /** @brief Create a query over entities matching the given query data. */
    template <query_data D>
    QueryState<D, Filter<>> query() {
        return world_->template query<D>();
    }
    /** @brief Create a filtered query over entities. */
    template <query_data D, query_filter F>
    QueryState<D, F> query_filtered() {
        return world_->template query_filtered<D, F>();
    }

   private:
    World* world_;
};
static_assert(std::movable<World>);
}  // namespace epix::ecs

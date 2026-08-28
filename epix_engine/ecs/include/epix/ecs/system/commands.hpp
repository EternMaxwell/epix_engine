#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <epix/common.hpp>
#include <epix/meta.hpp>
#include <optional>
#include <ranges>
#include <spdlog/spdlog.h>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/ecs/hierarchy/hierarchy.hpp>
#include <epix/ecs/storage.hpp>
#include <epix/ecs/system/param.hpp>
#include <epix/ecs/world.hpp>
#include <epix/ecs/world/commands.hpp>
#include <epix/ecs/world/entity_ref.hpp>

namespace epix::ecs {
template <>
struct SystemBuffer<internal::CommandQueue> {
    static void apply(internal::CommandQueue& buffer, const SystemMeta&, World& world) {
        world.flush_entities();
        world.flush_commands();
        buffer.apply(world);
    }
    static void queue(internal::CommandQueue& buffer, const SystemMeta&, DeferredWorld world) {
        world.command_queue().append(buffer);
    }
};
static_assert(system_param<Deferred<internal::CommandQueue>>);

EPIX_EXPORT struct EntityCommands;
/** @brief Deferred command interface for spawning/despawning entities and managing resources.
 *  Commands are queued and applied when the world is flushed. */
EPIX_EXPORT struct Commands {
   public:
    Commands(Deferred<internal::CommandQueue> queue, const Entities& entities) noexcept
        : command_queue(std::move(queue)), entities(&entities) {}

    /** @brief Append all commands from another CommandQueue. */
    void append(internal::CommandQueue& other) { command_queue->append(other); }
    /** @brief Queue a custom command for deferred execution. */
    template <typename T>
    void queue(T&& command)
        requires internal::valid_command<internal::Command<std::decay_t<T>>>
    {
        command_queue->push(std::forward<T>(command));
    }
    /** @brief Spawn a new empty entity and return an EntityCommands handle. */
    EntityCommands spawn_empty() noexcept;
    /** @brief Get an EntityCommands handle for an existing entity. */
    EntityCommands entity(Entity entity) noexcept;
    /** @brief Spawn a new entity with the given components. */
    template <typename... Ts>
    EntityCommands spawn(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...);

    /** @brief Construct and insert a resource via deferred command.
     *  Replaces existing resource of the same
     * type. */
    template <typename T, typename... Args>
        requires std::movable<T> && std::constructible_from<T, Args&&...>
    Commands& emplace_resource(Args&&... args) {
        untyped_vector vec(meta::type_info::of<T>(), 1);
        vec.emplace_back<T>(std::forward<Args>(args)...);
        replace_resource<T>(std::move(vec));
        return *this;
    }
    /** @brief Insert a resource by value via deferred command. */
    template <typename T>
        requires std::movable<std::decay_t<T>> && std::constructible_from<std::decay_t<T>, T>
    Commands& insert_resource(T&& value) {
        emplace_resource<std::decay_t<T>>(std::forward<T>(value));
        return *this;
    }
    /** @brief Construct and insert a resource only if it doesn't already exist. */
    template <typename T, typename... Args>
        requires std::movable<T> && std::constructible_from<T, Args&&...>
    Commands& try_emplace_resource(Args&&... args) {
        untyped_vector vec(meta::type_info::of<T>(), 1);
        vec.emplace_back<T>(std::forward<Args>(args)...);
        replace_resource<T>(std::move(vec), false);
        return *this;
    }
    /** @brief Insert a resource by value only if it doesn't already exist. */
    template <typename T>
        requires std::movable<std::decay_t<T>> && std::constructible_from<std::decay_t<T>, T>
    Commands& try_insert_resource(T&& value) {
        return try_emplace_resource<std::decay_t<T>>(std::forward<T>(value));
    }
    /** @brief Insert a range of `(Entity, bundle-or-component)` pairs through
     * one deferred command. This is the C++ counterpart of Bevy
     * `Commands::try_insert_batch`. A standalone component is the C++
     * counterpart of Rust's blanket `Component: Bundle` implementation.
     * Missing entities are reported at warning level when the command is
     * applied, like Bevy's fallible command. */
    template <std::ranges::input_range R>
        requires requires(std::ranges::range_value_t<R>& entry) {
            { entry.first } -> std::convertible_to<Entity>;
            requires std::movable<std::remove_cvref_t<decltype(entry.second)>>;
        }
    Commands& try_insert_batch(R&& entries) {
        using Entry = std::ranges::range_value_t<R>;
        std::vector<Entry> batch;
        if constexpr (std::ranges::sized_range<R>) batch.reserve(std::ranges::size(entries));
        for (auto& entry : entries) batch.emplace_back(std::move(entry));
        command_queue->push([batch = std::move(batch)](World& world) mutable {
            std::vector<Entity> missing_entities;
            for (auto& [entity, bundle] : batch) {
                auto entity_world = world.get_entity_mut(entity);
                if (!entity_world) {
                    missing_entities.push_back(entity);
                    continue;
                }
                if constexpr (is_bundle<decltype(bundle)>) {
                    entity_world->insert_bundle(std::move(bundle));
                } else {
                    entity_world->insert(std::move(bundle));
                }
            }
            if (!missing_entities.empty()) {
                spdlog::warn("[ecs] Could not insert a batch because {} target entities no longer exist.",
                             missing_entities.size());
            }
        });
        return *this;
    }
    /** @brief Remove a resource by type via deferred command. */
    template <typename T>
        requires std::movable<T>
    Commands& remove_resource() {
        command_queue->push([](World& world) { world.template remove_resource<T>(); });
        return *this;
    }

   private:
    template <typename T>
        requires std::movable<T>
    void replace_resource(untyped_vector data, bool replace_existing = true) {
        command_queue->push([data = std::move(data), replace_existing](World& world) mutable {
            if (!replace_existing && world.template get_resource<T>()) return;
            world.insert_resource(std::move(data.template get_as<T>(0)));
        });
    }

    Deferred<internal::CommandQueue> command_queue;
    const Entities* entities;
};
/** @brief Command interface for a specific entity.
 *
 * Provides deferred entity mutation methods (insert, remove, despawn, etc.)
 * that are queued and applied when the system stage flushes.
 */
EPIX_EXPORT struct EntityCommands {
   public:
    EntityCommands(Entity entity, Commands commands) noexcept : entity(entity), commands(std::move(commands)) {}

    /** @brief Insert one or more components into this entity.
     * @tparam Ts Component types (must be movable).
     * @return Reference to this EntityCommands for chaining.
     */
    template <typename... Ts>
    EntityCommands& insert(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...)
    {
        commands.queue([e = entity, comps = std::make_tuple(std::forward<Ts>(components)...)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                [&]<std::size_t... Is>(std::index_sequence<Is...>) mutable {
                    entity_world.insert(std::move(std::get<Is>(comps))...);
                }(std::make_index_sequence<sizeof...(Ts)>{});
                return true;
            });
        });
        return *this;
    }
    /** @brief Insert a bundle into this entity. */
    template <is_bundle T>
    EntityCommands& insert_bundle(T&& bundle) {
        commands.queue([e = entity, bundle = std::forward<T>(bundle)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                entity_world.insert_bundle(std::move(bundle));
                return true;
            });
        });
        return *this;
    }
    /** @brief Insert components only if not already present on this entity.
     * @tparam Ts Component types (must be movable).
     */
    template <typename... Ts>
    EntityCommands& insert_if_new(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...)
    {
        commands.queue([e = entity, comps = std::make_tuple(std::forward<Ts>(components)...)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                [&]<std::size_t... Is>(std::index_sequence<Is...>) mutable {
                    entity_world.insert_if_new(std::move(std::get<Is>(comps))...);
                }(std::make_index_sequence<sizeof...(Ts)>{});
                return true;
            });
        });
        return *this;
    }
    /** @brief Insert a bundle only if not already present on this entity. */
    template <is_bundle T>
    EntityCommands& insert_bundle_if_new(T&& bundle) {
        commands.queue([e = entity, bundle = std::forward<T>(bundle)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                entity_world.insert_bundle_if_new(std::move(bundle));
                return true;
            });
        });
        return *this;
    }
    /** @brief Remove components of the given types from this entity.
     * @tparam Ts Component types to remove.
     */
    template <typename... Ts>
    EntityCommands& remove() {
        commands.queue([e = entity](World& world) {
            world.get_entity_mut(e).and_then([](EntityWorldMut&& entity_world) -> std::optional<bool> {
                entity_world.remove<Ts...>();
                return true;
            });
        });
        return *this;
    }
    /** @brief Remove all components from this entity without despawning it. */
    EntityCommands& clear() {
        commands.queue([e = entity](World& world) {
            world.get_entity_mut(e).and_then([](EntityWorldMut&& entity_world) -> std::optional<bool> {
                entity_world.clear();
                return true;
            });
        });
        return *this;
    }
    /** @brief Despawn this entity, removing it from the world. */
    EntityCommands& despawn() {
        commands.queue([e = entity](World& world) {
            world.get_entity_mut(e).and_then([](EntityWorldMut&& entity_world) -> std::optional<bool> {
                entity_world.despawn();
                return true;
            });
        });
        return *this;
    }
    /** @brief Queue a custom command that receives an EntityWorldMut reference.
     * @tparam F A callable taking EntityWorldMut&.
     */
    template <std::invocable<EntityWorldMut&> F>
    EntityCommands& queue(F&& f) {
        commands.queue([e = entity, f = std::forward<F>(f)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) -> std::optional<bool> {
                f(entity_world);
                return true;
            });
        });
        return *this;
    }
    /** @brief Spawn a child entity with a Parent component pointing to this entity. */
    template <typename... Ts>
    EntityCommands spawn(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...)
    {
        return commands.spawn(Parent{entity}, std::forward<Ts>(components)...);
    }
    /** @brief Chain a callable that receives this EntityCommands for fluent building. */
    EntityCommands& then(std::invocable<EntityCommands&> auto&& func) {
        func(*this);
        return *this;
    }
    /** @brief Get the Entity id for this EntityCommands. */
    Entity id() const noexcept { return entity; }

   private:
    Entity entity;
    Commands commands;
};
template <typename... Ts>
inline EntityCommands Commands::spawn(Ts&&... components)
    requires(std::movable<std::decay_t<Ts>> && ...)
{
    if constexpr (sizeof...(Ts) == 0) {
        return spawn_empty();
    }
    Entity entity = entities->reserve_entity();
    return EntityCommands{entity, *this}.insert(std::forward<Ts>(components)...);
}

template <>
struct SystemParam<Commands> : SystemParam<std::tuple<Deferred<internal::CommandQueue>, const Entities&>> {
    using Base  = SystemParam<std::tuple<Deferred<internal::CommandQueue>, const Entities&>>;
    using State = typename Base::State;
    using Item  = Commands;
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        auto&& [deferred_queue, entities] = Base::get_param(state, meta, world, tick);
        return Commands(std::move(deferred_queue), entities);
    }
};
static_assert(system_param<Commands>);
}  // namespace epix::ecs

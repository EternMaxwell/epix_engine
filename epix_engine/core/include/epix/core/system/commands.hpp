#pragma once

#include "../hierarchy.hpp"
#include "../world/command_queue.hpp"
#include "param.hpp"

namespace epix::core::system {
template <>
struct SystemBuffer<CommandQueue> {
    static void apply(CommandQueue& buffer, const SystemMeta&, World& world) {
        world.flush_entities();
        world.flush_commands();
        buffer.apply(world);
    }
    static void queue(CommandQueue& buffer, const SystemMeta&, DeferredWorld world) {
        world.command_queue().append(buffer);
    }
};
static_assert(valid_system_param<SystemParam<Deferred<CommandQueue>>>);

struct EntityCommands;
struct Commands {
   public:
    Commands(Deferred<CommandQueue> queue, const Entities& entities)
        : command_queue(std::move(queue)), entities(&entities) {}

    void append(CommandQueue& other) { command_queue->append(other); }
    template <typename T>
    void queue(T&& command)
        requires valid_command<Command<std::decay_t<T>>>
    {
        command_queue->push(std::forward<T>(command));
    }
    EntityCommands spawn_empty();
    EntityCommands entity(Entity entity);
    template <typename... Ts>
    EntityCommands spawn(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...);

    template <typename T, typename... Args>
    Commands& emplace_resource(Args&&... args) {
        storage::untyped_vector vec(type_system::TypeInfo::get_info<T>(), 1);
        vec.emplace_back<T>(std::forward<Args>(args)...);
        replace_resource<T>(std::move(vec));
        return *this;
    }
    template <typename T>
    Commands& insert_resource(T&& value) {
        emplace_resource<std::decay_t<T>>(std::forward<T>(value));
        return *this;
    }
    template <typename T, typename... Args>
    Commands& try_emplace_resource(Args&&... args) {
        storage::untyped_vector vec(type_system::TypeInfo::get_info<T>(), 1);
        vec.emplace_back<T>(std::forward<Args>(args)...);
        replace_resource<T>(std::move(vec), false);
        return *this;
    }
    template <typename T>
    Commands& try_insert_resource(T&& value) {
        return try_emplace_resource<std::decay_t<T>>(std::forward<T>(value));
    }
    template <typename T>
    Commands& remove_resource() {
        command_queue->push([](World& world) {
            auto resource_id = world.type_registry().type_id<T>();
            world.storage_mut()
                .resources.get_mut(resource_id)
                .and_then([](storage::ResourceData& data) -> std::optional<bool> {
                    data.remove();
                    return true;
                });
        });
        return *this;
    }

   private:
    template <typename T>
    void replace_resource(storage::untyped_vector data, bool replace_existing = true) {
        command_queue->push([data = std::move(data), replace_existing](World& world) mutable {
            auto resource_id = world.type_registry().type_id<T>();
            if (!world.storage_mut().resources.initialize(resource_id) && !replace_existing) {
                return;
            }
            world.storage_mut()
                .resources.get_mut(resource_id)
                .and_then([&](storage::ResourceData& dest) -> std::optional<bool> {
                    dest.replace(world.change_tick(), std::move(data));
                    return true;
                });
        });
    }

    Deferred<CommandQueue> command_queue;
    const Entities* entities;
};
struct EntityCommands {
   public:
    EntityCommands(Entity entity, Commands commands) : entity(entity), commands(std::move(commands)) {}

    template <typename... Ts>
    EntityCommands& insert(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...)
    {
        commands.queue([e = entity, comps = std::make_tuple(std::forward<Ts>(components)...)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                [&]<size_t... Is>(std::index_sequence<Is...>) mutable {
                    entity_world.insert(std::move(std::get<Is>(comps))...);
                }(std::make_index_sequence<sizeof...(Ts)>{});
                return true;
            });
        });
        return *this;
    }
    template <typename... Ts>
    EntityCommands& insert_if_new(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...)
    {
        commands.queue([e = entity, comps = std::make_tuple(std::forward<Ts>(components)...)](World& world) mutable {
            world.get_entity_mut(e).and_then([&](EntityWorldMut&& entity_world) mutable -> std::optional<bool> {
                [&]<size_t... Is>(std::index_sequence<Is...>) mutable {
                    entity_world.insert_if_new(std::move(std::get<Is>(comps))...);
                }(std::make_index_sequence<sizeof...(Ts)>{});
                return true;
            });
        });
        return *this;
    }
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
    EntityCommands& clear() {
        commands.queue([e = entity](World& world) {
            world.get_entity_mut(e).and_then([](EntityWorldMut&& entity_world) -> std::optional<bool> {
                entity_world.clear();
                return true;
            });
        });
        return *this;
    }
    EntityCommands& despawn() {
        commands.queue([e = entity](World& world) {
            world.get_entity_mut(e).and_then([](EntityWorldMut&& entity_world) -> std::optional<bool> {
                entity_world.despawn();
                return true;
            });
        });
        return *this;
    }
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
    template <typename... Ts>
    EntityCommands spawn(Ts&&... components)
        requires(std::movable<std::decay_t<Ts>> && ...)
    {
        return commands.spawn(hierarchy::Parent{entity}, std::forward<Ts>(components)...);
    }
    EntityCommands& then(std::invocable<EntityCommands&> auto&& func) {
        func(*this);
        return *this;
    }

   private:
    Entity entity;
    Commands commands;
};
inline EntityCommands Commands::spawn_empty() {
    Entity entity = entities->reserve_entity();
    return EntityCommands{entity, *this};
}
inline EntityCommands Commands::entity(Entity entity) { return EntityCommands{entity, *this}; }
template <typename... Ts>
inline EntityCommands Commands::spawn(Ts&&... components)
    requires(std::movable<std::decay_t<Ts>> && ...)
{
    if constexpr (sizeof...(Ts) == 0) {
        return spawn_empty();
    }
}

template <>
struct SystemParam<Commands> : SystemParam<std::tuple<Deferred<CommandQueue>, const Entities&>> {
    using Base  = SystemParam<std::tuple<Deferred<CommandQueue>, const Entities&>>;
    using State = typename Base::State;
    using Item  = Commands;
    static Item get_param(State& state, const SystemMeta& meta, World& world, Tick tick) {
        auto&& [deferred_queue, entities] = Base::get_param(state, meta, world, tick);
        return Commands(std::move(deferred_queue), entities);
    }
};
}  // namespace epix::core::system
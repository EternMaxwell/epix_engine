#pragma once

#include "systemparam.h"
#include "world.h"

namespace epix::app {
struct DespawnCommand {
    Entity entity;

    EPIX_API void apply(World& world);
};
struct DespawnRecurseCommand {
    Entity entity;

    EPIX_API void apply(World& world);
};
struct RemoveResourceCommand {
    std::type_index type;

    EPIX_API void apply(World& world);
};
struct EntityEraseCommand {
    void (*func)(World*, Entity);
    Entity entity;

    EPIX_API void apply(World& world);
};

struct EntityCommands {
   private:
    World* world;
    CommandQueue* queue;
    Entity entity;

    EntityCommands(World& world, CommandQueue& queue, Entity entity)
        : world(&world), queue(&queue), entity(entity) {}

   public:
    friend struct Commands;

    operator Entity() const { return entity; }

    template <typename T, typename... Args>
    void emplace(Args&&... args) {
        world->entity_emplace<T>(entity, std::forward<Args>(args)...);
    };
    template <typename T>
    void emplace(T&& obj) {
        world->entity_emplace<std::decay_t<T>>(entity, std::forward<T>(obj));
    };
    template <typename... Args>
    void erase() {
        queue->enqueue<EntityEraseCommand>(
            [](World* world, Entity entity) {
                world->entity_erase<Args...>(entity);
            },
            entity
        );
    };
    template <typename... Args>
    EntityCommands spawn(Args&&... args) {
        auto new_entity =
            world->spawn(Parent{entity}, std::forward<Args>(args)...);
        world->entity_get_or_emplace<Children>(entity).entities.emplace(
            new_entity
        );
        return EntityCommands(*world, *queue, new_entity);
    };
    EPIX_API void despawn() { queue->enqueue<DespawnCommand>(entity); };
    EPIX_API void despawn_recurse() {
        queue->enqueue<DespawnRecurseCommand>(entity);
    };
};
struct Commands {
   private:
    World* world;
    CommandQueue* queue;

   public:
    EPIX_API Commands(World& world);

    EPIX_API EntityCommands entity(Entity entity);
    template <typename... Args>
    EntityCommands spawn(Args&&... args) {
        auto entity = world->spawn(std::forward<Args>(args)...);
        return EntityCommands(*world, *queue, entity);
    };
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        world->emplace_resource<T>(std::forward<Args>(args)...);
    };
    template <typename T>
    void init_resource() {
        world->init_resource<T>();
    };
    template <typename T>
    void insert_resource(T&& res) {
        world->insert_resource(std::forward<T>(res));
    };
    template <typename T>
    void remove_resource() {
        using type = std::decay_t<T>;
        queue->enqueue<RemoveResourceCommand>(typeid(type));
    };
};

template <>
struct SystemParam<Commands> {
    using State = Commands;
    State init(World& world, SystemMeta& meta) {
        meta.access.commands = true;
        return Commands(world);
    }
    bool update(State& state, World& world, const SystemMeta& meta) {
        state = Commands(world);
        return true;
    }
    Commands& get(State& state) { return state; }
};
static_assert(
    ValidParam<Commands>, "Commands should be a valid parameter for SystemParam"
);
}  // namespace epix::app
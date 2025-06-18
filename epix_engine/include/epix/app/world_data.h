#pragma once

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>
#include <entt/entity/registry.hpp>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <typeindex>

#include "entity.h"
#include "epix/common.h"
#include "epix/utils/command_queue.h"
#include "hash_tool.h"

namespace epix::app {
struct World;
struct Parent {
    Entity entity;
};
struct Children {
    entt::dense_set<Entity> entities;
};
}  // namespace epix::app

namespace epix::app {
/**
 * @brief `UntypedRes` is a type-erased resource that can be used to store any
 * type of resource in the world. It contains a type index, a shared pointer to
 * the resource, and a shared mutex for thread safety.
 *
 * Constructor is protected to prevent direct instantiation, but copy and move
 * are still allowed. Use the static `create` method to create an instance of
 * `UntypedRes` instead.
 */
struct UntypedRes {
    std::type_index type;
    std::shared_ptr<void> resource;
    std::shared_ptr<std::shared_mutex> mutex;

   protected:
    UntypedRes(
        const std::type_index& type,
        std::shared_ptr<void> resource,
        std::shared_ptr<std::shared_mutex> mutex
    )
        : type(type), resource(resource), mutex(mutex) {}

   public:
    static UntypedRes create(
        const std::type_index& type,
        std::shared_ptr<void> resource,
        std::shared_ptr<std::shared_mutex> mutex =
            std::make_shared<std::shared_mutex>()
    ) {
        return UntypedRes{type, resource, mutex};
    }
    template <typename T>
    static UntypedRes create(
        std::shared_ptr<T> resource,
        std::shared_ptr<std::shared_mutex> mutex =
            std::make_shared<std::shared_mutex>()
    ) {
        return UntypedRes{
            typeid(T), std::static_pointer_cast<void>(resource), mutex
        };
    }
    template <typename T, typename... Args>
    static UntypedRes emplace(Args&&... args) {
        return UntypedRes{
            typeid(T), std::make_shared<T>(std::forward<Args>(args)...),
            std::make_shared<std::shared_mutex>()
        };
    }
    template <typename T>
    static UntypedRes emplace(T&& resource) {
        using type = std::decay_t<T>;
        return UntypedRes{
            typeid(type), std::make_shared<type>(std::forward<T>(resource)),
            std::make_shared<std::shared_mutex>()
        };
    }
};
using CommandQueue = epix::utils::CommandQueue<World&>;
struct WorldData {
    entt::registry registry;
    entt::dense_map<std::type_index, UntypedRes> resources;
    mutable std::shared_mutex resources_mutex;
};
}  // namespace epix::app
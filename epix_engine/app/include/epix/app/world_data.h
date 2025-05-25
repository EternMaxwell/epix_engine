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
template <typename T>
concept IsCommand = requires(T t) {
    { t.apply(std::declval<World&>()) };
};
/**
 * @brief A CommandQueue to store delayed World operations that will be executed
 * later. Typically at the end of the current `schedule`.
 *
 * This is used for thread safety, as the world is not thread safe. The command
 * queue is flushed at the end of the current schedule, and all commands are
 * executed in order.
 */
struct CommandQueue {
   private:
    struct Command {
        std::type_index type;
        size_t size;
        void (*apply)(World&, void*);
        void (*destruct)(void*);
    };

    entt::dense_map<std::type_index, std::uint8_t> m_command_map;
    std::vector<Command> m_registry;

    // the data stored as a byte array
    std::vector<std::uint8_t> m_commands;
    // mutex for allowing thread-safe access to the command queue
    std::mutex m_mutex;

    template <IsCommand T, typename... Args>
    void enqueue_internal(Args&&... args) {
        using type = T;
        std::unique_lock lock(m_mutex);
        auto it       = m_command_map.find(typeid(type));
        uint8_t index = 0;
        if (it == m_command_map.end()) {
            if (m_registry.size() >= 256) {
                throw std::runtime_error(
                    "Command queue can not accept more than 256 command types."
                );
            }
            m_command_map.emplace(
                typeid(type), static_cast<uint8_t>(m_registry.size())
            );
            index = m_registry.size();
            m_registry.emplace_back(
                typeid(type), sizeof(type),
                [](World& world, void* command) {
                    auto* cmd = reinterpret_cast<type*>(command);
                    cmd->apply(world);
                },
                [](void* command) {
                    auto* cmd = reinterpret_cast<type*>(command);
                    cmd->~type();
                }
            );
        } else {
            index = it->second;
        }
        m_commands.push_back(index);
        m_commands.resize(m_commands.size() + sizeof(type));
        auto* pcommand = reinterpret_cast<type*>(
            m_commands.data() + m_commands.size() - sizeof(type)
        );
        // construct the command in place
        new (pcommand) type(std::forward<Args>(args)...);
    }

   public:
    CommandQueue()                               = default;
    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue(CommandQueue&&)                 = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue& operator=(CommandQueue&&)      = delete;
    ~CommandQueue()                              = default;

    EPIX_API void flush(World& world);
    template <typename T, typename... Args>
        requires IsCommand<T>
    void enqueue(Args&&... args) {
        enqueue_internal<std::decay_t<T>>(std::forward<Args>(args)...);
    }
    template <typename T>
        requires IsCommand<std::decay_t<T>>
    void enqueue(T&& command) {
        enqueue_internal<std::decay_t<T>>(std::forward<T>(command));
    }
};
struct WorldData {
    entt::registry registry;
    entt::dense_map<std::type_index, UntypedRes> resources;
    mutable std::shared_mutex resources_mutex;
};
}  // namespace epix::app
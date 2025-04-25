#pragma once

#include <epix/utils/core.h>

#include <entt/container/dense_map.hpp>
#include <entt/container/dense_set.hpp>
#include <entt/entity/registry.hpp>
#include <shared_mutex>

#include "tool.h"

// pre-declare
namespace epix::app {
using entt::dense_map;
using entt::dense_set;
using tools::Label;

struct Entity;
struct World;
template <typename T>
struct GetWorldParam;
};  // namespace epix::app

// define
namespace epix::app {
struct CommandQueue {
   private:
    std::vector<Entity> m_despawn_list;                    // 0
    std::vector<Entity> m_recurse_despawn_list;            // 1
    std::vector<std::type_index> m_remove_resources_list;  // 2
    std::vector<std::pair<void (*)(World*, Entity), Entity>>
        m_entity_erase_list;  // 3

    std::vector<uint32_t>
        m_commands;  // 4 bytes per command, with first byte indicating the
                     // command type: 0: despawn, 1: despawn_recurse, 2:
                     // remove_resource, 3: entity_erase. The rest of the bytes
                     // are the index of the actual command in the list.

    std::mutex m_mutex;

   public:
    CommandQueue()                               = default;
    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue(CommandQueue&&)                 = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue& operator=(CommandQueue&&)      = delete;
    ~CommandQueue()                              = default;

    EPIX_API void flush(World& world);
    EPIX_API void despawn(const Entity& entity);
    EPIX_API void despawn_recurse(const Entity& entity);
    EPIX_API void remove_resource(const std::type_index& type);
    EPIX_API void entity_erase(
        void (*func)(World*, Entity), const Entity& entity
    );
};

template <typename T>
concept is_bundle = requires(T t) {
    { t.unpack() };
};

struct UntypedRes {
    std::shared_ptr<void> resource;
    std::shared_ptr<std::shared_mutex> mutex;

    EPIX_API operator bool() const { return resource != nullptr; }
    EPIX_API bool operator!() const { return resource == nullptr; }
};

struct World {
   private:
    entt::registry m_registry;
    dense_map<std::type_index, UntypedRes> m_resources;
    mutable std::shared_mutex m_resources_mutex;
    CommandQueue m_command_queue;

   public:
    EPIX_API World();
    World(const World&)            = delete;
    World(World&&)                 = delete;
    World& operator=(const World&) = delete;
    World& operator=(World&&)      = delete;
    EPIX_API ~World();

    // template <typename T>
    // std::pair<T&, std::shared_lock<std::shared_mutex>> resource_mut() {
    //     std::shared_lock lock(m_resources_mutex);
    //     auto it = m_resources.find(typeid(T));
    //     if (it != m_resources.end()) {
    //         return {
    //             *std::static_pointer_cast<T>(it->second.resource),
    //             std::shared_lock<std::shared_mutex>(*it->second.mutex)
    //         };
    //     }
    //     throw std::runtime_error("Resource not found");
    // }
    // template <typename T>
    // std::pair<const T&, std::unique_lock<std::shared_mutex>> resource() const {
    //     std::shared_lock lock(m_resources_mutex);
    //     auto it = m_resources.find(typeid(T));
    //     if (it != m_resources.end()) {
    //         return {
    //             *std::static_pointer_cast<T>(it->second.resource),
    //             std::unique_lock<std::shared_mutex>(*it->second.mutex)
    //         };
    //     }
    //     throw std::runtime_error("Resource not found");
    // }
    // template <typename T>
    // std::pair<const T*, std::shared_lock<std::shared_mutex>> get_resource(
    // ) const {
    //     std::unique_lock lock(m_resources_mutex);
    //     auto it = m_resources.find(typeid(T));
    //     if (it != m_resources.end()) {
    //         return {
    //             std::static_pointer_cast<T>(it->second.resource).get(),
    //             std::shared_lock<std::shared_mutex>(*it->second.mutex)
    //         };
    //     }
    //     return {nullptr, std::shared_lock<std::shared_mutex>()};
    // }
    // template <typename T>
    // std::pair<T*, std::unique_lock<std::shared_mutex>> get_resource_mut() {
    //     std::unique_lock lock(m_resources_mutex);
    //     auto it = m_resources.find(typeid(T));
    //     if (it != m_resources.end()) {
    //         return {
    //             std::static_pointer_cast<T>(it->second.resource).get(),
    //             std::unique_lock<std::shared_mutex>(*it->second.mutex)
    //         };
    //     }
    //     return {nullptr, std::unique_lock<std::shared_mutex>()};
    // }
    EPIX_API UntypedRes resource(const std::type_index& type) const;

    template <typename T>
    void init_resource() {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(std::decay_t<T>),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::decay_t<T>>()
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void insert_resource(T&& res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(std::decay_t<T>),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::decay_t<T>>(std::forward<T>(res))
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(std::decay_t<T>),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::make_shared<std::decay_t<T>>(
                            std::forward<Args>(args)...
                        )
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void add_resource(const std::shared_ptr<T>& res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(std::decay_t<T>),
                UntypedRes{
                    std::static_pointer_cast<void>(res),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void add_resource(T* res) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(std::decay_t<T>),
                UntypedRes{
                    std::static_pointer_cast<void>(
                        std::shared_ptr<std::decay_t<T>>(res)
                    ),
                    std::make_shared<std::shared_mutex>()
                }
            );
        }
    }
    template <typename T>
    void add_resource(
        std::shared_ptr<T> res, std::shared_ptr<std::shared_mutex> mutex
    ) {
        std::unique_lock lock(m_resources_mutex);
        if (!m_resources.contains(typeid(std::decay_t<T>))) {
            m_resources.emplace(
                typeid(std::decay_t<T>),
                UntypedRes{std::static_pointer_cast<void>(res), mutex}
            );
        }
    }
    EPIX_API void add_resource(std::type_index type, UntypedRes res);
    EPIX_API void add_resource(std::type_index type, std::shared_ptr<void> res);
    template <typename T>
    void remove_resource() {
        std::unique_lock lock(m_resources_mutex);
        auto it = m_resources.find(typeid(T));
        if (it != m_resources.end()) {
            m_resources.erase(it);
        }
    }
    EPIX_API void remove_resource(const std::type_index& type);

    template <typename T, typename... Args>
        requires std::constructible_from<std::decay_t<T>, Args...> ||
                 is_bundle<T>
    void entity_emplace(Entity entity, Args&&... args) {
        using type = std::decay_t<T>;
        if constexpr (is_bundle<T>) {
            auto&& bundle = T(std::forward<Args>(args)...);
            entity_emplace_tuple(entity, std::move(bundle.unpack()));
        } else {
            m_registry.emplace<std::decay_t<T>>(
                entity, std::forward<Args>(args)...
            );
        }
    }
    template <typename... Args>
    void entity_emplace_tuple(Entity entity, std::tuple<Args...>&& args) {
        entity_emplace_tuple(
            entity, std::move(args), std::index_sequence_for<Args...>()
        );
    }
    template <typename... Args, size_t... I>
    void
    entity_emplace_tuple(Entity entity, std::tuple<Args...>&& args, std::index_sequence<I...>) {
        (entity_emplace<decltype(std::get<I>(args))>(
             entity, std::forward<std::tuple_element_t<I, std::tuple<Args...>>>(
                         std::get<I>(args)
                     )
         ),
         ...);
    }
    template <typename... Args>
    void entity_erase(Entity entity) {
        m_registry.remove<Args...>(entity);
    }
    template <typename... Args>
    Entity spawn(Args&&... args) {
        auto entity = m_registry.create();
        (entity_emplace<Args>(Entity{entity}, std::forward<Args>(args)), ...);
        return Entity{entity};
    }

    template <typename T>
    friend struct GetWorldParam;
    friend struct CommandQueue;
    friend struct App;
    friend struct Schedule;
    friend struct Commands;
    friend struct EntityCommands;
};
}  // namespace epix::app
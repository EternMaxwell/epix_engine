#pragma once

#include <expected>

#include "epix/utils/core.h"
#include "label.h"
#include "world_data.h"

namespace epix::app {
struct WorldLabel : public Label {
    using Label::Label;
    template <typename T>
    WorldLabel(T t) : Label(t) {}
};
struct World;
}  // namespace epix::app

namespace epix::app {
/**
 * @brief Concept `FromWorld` describes if a type (as resource in world) can
 * be created from a world. It is used to check if a type can be created from a
 * world.
 *
 * If `T` has a static method `from_world(World&)` which returns `T`, or if `T`
 * can be constructed from `World&`, then FromWorld is satisfied.
 *
 * @tparam `T` is the type to be checked.
 */
template <typename T>
concept FromWorld = requires(T t) {
    { T::from_world(std::declval<World&>()) } -> std::same_as<T>;
} || std::constructible_from<T, World&>;
template <typename T>
concept OptFromWorld = requires(T t) {
    { T::from_world(std::declval<World&>()) } -> std::same_as<std::optional<T>>;
};
/**
 * @brief An Empty struct that used to be derived from if a type is a bundle.
 *
 * A type is a bundle if it is derived from `Bundle` and has a `unpack`
 * method that returns a tuple of the types in the bundle. In this case, when
 * spawning entity with bundle, the components added will be the types in the
 * tuple.
 */
struct Bundle {};
/**
 * @brief A type is a bundle if it is derived from `Bundle` and has a `unpack`
 * method that returns a tuple of the types in the bundle.
 */
template <typename T>
concept is_bundle = requires(T t) {
    { t.unpack() } -> epix::util::type_traits::specialization_of<std::tuple>;
    std::derived_from<T, Bundle>;
};
}  // namespace epix::app

namespace epix::app {
/**
 * @brief World is where all the entities, components, and resources
 * are managed.
 *
 * World is not thread-safe by itself. But the command queue is
 * still thread-safe by itself.
 */
struct World {
   private:
    const WorldLabel m_label;
    WorldData m_data;
    CommandQueue m_command_queue;

    template <typename... Args>
    void entity_emplace_tuple(Entity entity, std::tuple<Args...>&& args) {
        entity_emplace_tuple(
            entity, std::move(args), std::index_sequence_for<Args...>()
        );
    }
    template <typename... Args, size_t... I>
    void
    entity_emplace_tuple(Entity entity, std::tuple<Args...>&& args, std::index_sequence<I...>) {
        (entity_emplace(entity, std::forward<Args>(std::get<I>(args))), ...);
    }

   public:
    EPIX_API World(const WorldLabel& label);
    World(const World&)            = delete;
    World(World&&)                 = delete;
    World& operator=(const World&) = delete;
    World& operator=(World&&)      = delete;
    ~World()                       = default;

    EPIX_API CommandQueue& command_queue();
    EPIX_API entt::registry& registry();

    // resource part
    /**
     * @brief Add a resource to the world use default constructor or
     * `from_world` if this type satisfies `FromWorld` concept, e.g. has a
     * static method `from_world(World&)` which returns it self or can be
     * constructed from `World&`. This requires it to be moveable or copyable.
     *
     * Note that the actual type added is `std::decay_t<T>`. And if the resource
     * already exists, this function does nothing.
     *
     * It is guaranteed that `UntypedRes::type` is the same as
     * `typeid(std::decay_t<T>)`.
     *
     * @tparam T the type of the resource to be added.
     */
    template <typename T>
    void init_resource() {
        auto resources = m_data.resources.write();
        using type     = std::decay_t<T>;
        if (!resources->contains(typeid(type))) {
            if constexpr (FromWorld<type>) {
                if constexpr (std::constructible_from<type, World&>) {
                    resources->emplace(
                        typeid(type), std::make_shared<type>(*this)
                    );
                } else {
                    resources->emplace(
                        typeid(type),
                        std::make_shared<type>(type::from_world(*this))
                    );
                }
            } else {
                resources->emplace(typeid(type), std::make_shared<type>());
            }
        }
    };
    /**
     * @brief Insert a resource to the world using copy or move constructor.
     *
     * This function does nothing if the resource already exists.
     *
     * It is guaranteed that `UntypedRes::type` is the same as
     * `typeid(std::decay_t<T>)`.
     *
     * @param res The resource to be inserted.
     */
    template <typename T>
    void insert_resource(T&& res) {
        using type     = std::decay_t<T>;
        auto resources = m_data.resources.write();
        if (!resources->contains(typeid(type))) {
            resources->emplace(
                typeid(type), std::make_shared<type>(std::forward<T>(res))
            );
        }
    };
    template <typename T>
    void add_resource(std::shared_ptr<T>&& res) {
        using type     = std::decay_t<T>;
        auto resources = m_data.resources.write();
        if (!resources->contains(typeid(type))) {
            resources->emplace(typeid(type), std::move(res));
        }
    };
    EPIX_API void add_resource(
        std::type_index type, std::shared_ptr<void>&& res
    ) {
        auto resources = m_data.resources.write();
        if (!resources->contains(type)) {
            resources->emplace(type, std::move(res));
        }
    };
    /**
     * @brief Insert a resource to the world using construct in place.
     *
     * This function does nothing if the resource already exists.
     *
     * It is guaranteed that `UntypedRes::type` is the same as
     * `typeid(std::decay_t<T>)`.
     *
     * @tparam T the type of the resource to be inserted. Will be decayed
     * automatically.
     * @param args The arguments to be passed to the constructor of the
     * resource.
     */
    template <typename T, typename... Args>
    void emplace_resource(Args&&... args) {
        using type     = std::decay_t<T>;
        auto resources = m_data.resources.write();
        if (!resources->contains(typeid(type))) {
            resources->emplace(
                typeid(type),
                std::make_shared<type>(std::forward<Args>(args)...)
            );
        }
    };
    /**
     * @brief Immediately remove a resource from the world of the given type.
     *
     * Note that the type is required to be decayed.
     *
     * @param type The type_index of the resource to be removed.
     */
    EPIX_API void remove_resource(const std::type_index& type);
    template <typename T>
    T& resource() {
        auto resources = m_data.resources.read();
        if (auto it = resources->find(typeid(T)); it != resources->end()) {
            return *std::static_pointer_cast<T>(it->second);
        } else {
            throw std::runtime_error("Resource not found.");
        }
    }
    template <typename T>
    const T& resource() const {
        auto resources = m_data.resources.read();
        if (auto it = resources->find(typeid(T)); it != resources->end()) {
            return *std::static_pointer_cast<T>(it->second);
        } else {
            throw std::runtime_error("Resource not found.");
        }
    }
    template <typename T>
    T* get_resource() {
        auto resources = m_data.resources.read();
        if (auto it = resources->find(typeid(T)); it != resources->end()) {
            return std::static_pointer_cast<T>(it->second).get();
        } else {
            return nullptr;
        }
    }
    template <typename T>
    const T* get_resource() const {
        auto resources = m_data.resources.read();
        if (auto it = resources->find(typeid(T)); it != resources->end()) {
            return std::static_pointer_cast<T>(it->second).get();
        } else {
            return nullptr;
        }
    }
    // entity component part
    /**
     * @brief Spawn an entity with the given components.
     *
     * If an arg is a bundle, the components will be unpacked and added to the
     * entity.
     *
     * @return Entity The id of the spawned entity.
     */
    template <typename... Args>
    Entity spawn(Args&&... args) {
        Entity id = m_data.registry.create();
        if constexpr (sizeof...(Args) > 0) {
            (entity_emplace<Args>(id, std::forward<Args>(args)), ...);
        }
        return id;
    }
    /**
     * @brief Immediately despawn an entity. This will remove all components and
     * remove the entity from the world.
     *
     * @param entity The entity to be despawned.
     */
    EPIX_API void despawn(Entity entity);
    /**
     * @brief Add a component to an entity using in place constructing. If the
     * entity is not valid, this function does nothing.
     *
     * If the component is a bundle, the components will be unpacked and added
     * to the entity.
     *
     * If the component already exists, it will be replaced with the new one.
     *
     * @tparam T The type of the component to be added. This will be decayed
     * automatically.
     * @param entity The entity to be added the component to.
     * @param args The arguments to be passed to the constructor of the
     * component.
     */
    template <typename T, typename... Args>
        requires std::constructible_from<std::decay_t<T>, Args...>
    void entity_emplace(Entity entity, Args&&... args) {
        if (!m_data.registry.valid(entity)) return;
        using type = std::decay_t<T>;
        if constexpr (is_bundle<T>) {
            entity_emplace_tuple(
                entity, T(std::forward<Args>(args)...).unpack()
            );
        } else {
            m_data.registry.emplace_or_replace<std::decay_t<T>>(
                entity, std::forward<Args>(args)...
            );
        }
    }
    /**
     * @brief Add component of type `std::decay_t<T>` to the entity. This will
     * replace the component if it already exists.
     *
     * @param entity The entity to be added the component to.
     * @param obj The object to be added to the entity.
     */
    template <typename T>
    void entity_emplace(Entity entity, T&& obj) {
        if (!m_data.registry.valid(entity)) return;
        using type = std::decay_t<T>;
        if constexpr (is_bundle<T>) {
            entity_emplace_tuple(entity, obj.unpack());
        } else {
            m_data.registry.emplace_or_replace<type>(
                entity, std::forward<T>(obj)
            );
        }
    }
    /**
     * @brief Erase components of the given types from the entity. If the entity
     * is not valid, this function does nothing.
     *
     * If the entity does not have the component, this function does nothing.
     *
     * @param entity The entity to be erased the component from.
     * @tparam Args The types of the components to be erased.
     */
    template <typename... Args>
    void entity_erase(Entity entity) {
        if (!m_data.registry.valid(entity)) return;
        m_data.registry.remove<Args...>(entity);
    }
    /**
     * @brief Check if the entity is valid.
     *
     * @param entity The entity to be checked.
     * @return true if the entity is valid, false otherwise.
     */
    EPIX_API bool entity_valid(Entity entity);
    template <typename T>
    T& entity_get(Entity entity) {
        auto* p = entity_try_get<T>(entity);
        if (!p) throw std::runtime_error("Entity does not have component");
        return m_data.registry.get<T>(entity);
    }
    template <typename T>
    T* entity_try_get(Entity entity) {
        if (!m_data.registry.valid(entity)) return nullptr;
        return m_data.registry.try_get<T>(entity);
    }
    template <typename T, typename... Args>
    T& entity_get_or_emplace(Entity entity, Args&&... args) {
        if (!m_data.registry.valid(entity))
            throw std::runtime_error("Entity is not valid");
        return m_data.registry.get_or_emplace<T>(
            entity, std::forward<Args>(args)...
        );
    }
};
}  // namespace epix::app
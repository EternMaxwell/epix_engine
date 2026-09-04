#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#endif

namespace epix::render::sync_world {
/**
 * @brief Marker component: the entity must be synchronized to the render world
 * (Bevy `SyncToRenderWorld`).
 */
EPIX_EXPORT struct SyncToRenderWorld {
    static void on_add(ecs::World& world, ecs::HookContext context);
    static void on_remove(ecs::World& world, ecs::HookContext context);
};
/**
 * @brief Component stored on a render-world entity pointing back to its
 * main-world entity (Bevy `MainEntity`).
 */
EPIX_EXPORT struct MainEntity {
   private:
    ecs::Entity m_entity;

   public:
    explicit constexpr MainEntity(ecs::Entity entity) noexcept : m_entity(entity) {}

    /** @brief The corresponding main-world entity (Bevy `MainEntity::id`). */
    constexpr ecs::Entity id() const noexcept { return m_entity; }

    bool operator==(const MainEntity&) const = default;
};

/**
 * @brief Component stored on a main-world entity pointing to its render-world
 * entity (Bevy `RenderEntity`).
 */
EPIX_EXPORT struct RenderEntity {
   private:
    ecs::Entity m_entity;

   public:
    explicit constexpr RenderEntity(ecs::Entity entity) noexcept : m_entity(entity) {}

    /** @brief The corresponding render-world entity (Bevy `RenderEntity::id`). */
    constexpr ecs::Entity id() const noexcept { return m_entity; }

    bool operator==(const RenderEntity&) const = default;
};

/** @brief Map keyed by main-world entity (Bevy `MainEntityHashMap<V>`). */
template <typename V>
using MainEntityHashMap = std::unordered_map<MainEntity, V, std::hash<MainEntity>>;

/** @brief Set of main-world entities (Bevy `MainEntityHashSet`). */
using MainEntityHashSet = std::unordered_set<MainEntity, std::hash<MainEntity>>;

/**
 * @brief Marker placed on render-world entities that are despawned at the end
 * of each frame in `RenderSystems::PostCleanup` (Bevy `TemporaryRenderEntity`).
 */
EPIX_EXPORT struct TemporaryRenderEntity {};

/** @brief Lifecycle record queued for render-world entity synchronization
 * (Bevy `EntityRecord`). */
EPIX_EXPORT struct EntityAdded {
    ecs::Entity entity;
};
EPIX_EXPORT struct EntityRemoved {
    RenderEntity entity;
};
EPIX_EXPORT struct ComponentRemoved {
    ecs::Entity entity;
};
using EntityRecord = std::variant<EntityAdded, EntityRemoved, ComponentRemoved>;

/** @brief Main-world resource collecting lifecycle records for the next
 * extraction step (Bevy `PendingSyncEntity`). */
EPIX_EXPORT struct PendingSyncEntity {
    std::vector<EntityRecord> records;
};

/** @brief Component remove hook used by `SyncComponentPlugin<C>` (Bevy's
 * `on_remove` registration in `SyncComponentPlugin`). */
EPIX_EXPORT void record_component_removed(ecs::World& world, ecs::HookContext context);

/**
 * @brief Synchronizes entities marked with SyncToRenderWorld between the main
 * and render worlds (Bevy `entity_sync_system`). Called at the start of
 * extraction each frame. It consumes `PendingSyncEntity` lifecycle records,
 * creating, despawning, or recreating render entities as Bevy does.
 */
EPIX_EXPORT void entity_sync_system(ecs::World& main_world, ecs::World& render_world);

/**
 * @brief Despawns render entities marked TemporaryRenderEntity (Bevy
 * `despawn_temporary_render_entities`). Runs in `RenderSystems::PostCleanup`.
 */
EPIX_EXPORT void remove_temporary_render_entities(ecs::World& world);

/**
 * @brief Plugin that keeps entities with SyncToRenderWorld synchronized
 * between the main and render worlds (Bevy `SyncWorldPlugin`). Attached by
 * RenderPlugin; the sync step itself is invoked by the render app's extract
 * function.
 */
EPIX_EXPORT struct SyncWorldPlugin {
    void attach(app::App& app);
};

}  // namespace epix::render::sync_world

/** @brief Hash for `MainEntity` (Bevy derives Hash on the newtype). */
template <>
struct std::hash<::epix::render::sync_world::MainEntity> {
    std::size_t operator()(const ::epix::render::sync_world::MainEntity& e) const noexcept {
        return std::hash<::epix::ecs::Entity>{}(e.id());
    }
};

/** @brief Hash for `RenderEntity` (Bevy derives Hash on the newtype). */
template <>
struct std::hash<::epix::render::sync_world::RenderEntity> {
    std::size_t operator()(const ::epix::render::sync_world::RenderEntity& e) const noexcept {
        return std::hash<::epix::ecs::Entity>{}(e.id());
    }
};

namespace epix::ecs {
// Bevy queries `RenderEntity` by value as a query data item (camera.rs
// `Query<(Entity, RenderEntity, ...)>`); Epix mirrors that with a by-value
// `QueryData<RenderEntity>` (Item = RenderEntity), reading the component
// read-only but returning a copy, so queries can use the value rather than a
// `const RenderEntity&` reference.
template <>
struct WorldQuery<::epix::render::sync_world::RenderEntity>
    : WorldQuery<const ::epix::render::sync_world::RenderEntity&> {};
template <>
struct QueryData<::epix::render::sync_world::RenderEntity> {
    using Item                            = ::epix::render::sync_world::RenderEntity;
    using ReadOnly                        = ::epix::render::sync_world::RenderEntity;
    static inline constexpr bool readonly = true;
    static Item fetch(typename WorldQuery<const ::epix::render::sync_world::RenderEntity&>::Fetch& fetch,
                      ::epix::ecs::Entity entity, TableRow row) noexcept {
        return QueryData<const ::epix::render::sync_world::RenderEntity&>::fetch(fetch, entity, row);
    }
};
static_assert(query_data<::epix::render::sync_world::RenderEntity>);
}  // namespace epix::ecs

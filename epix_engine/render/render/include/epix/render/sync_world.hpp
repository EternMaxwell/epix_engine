#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#endif

namespace epix::render::sync_world {
/**
 * @brief Marker component: the entity must be synchronized to the render world
 * (Bevy `SyncToRenderWorld`).
 */
EPIX_EXPORT struct SyncToRenderWorld {};
/**
 * @brief Component stored on a render-world entity pointing back to its
 * main-world entity (Bevy `MainEntity`).
 */
EPIX_EXPORT struct MainEntity {
    ecs::Entity entity;

    bool operator==(const MainEntity&) const = default;
};

/**
 * @brief Component stored on a main-world entity pointing to its render-world
 * entity (Bevy `RenderEntity`).
 */
EPIX_EXPORT struct RenderEntity {
    ecs::Entity entity;

    bool operator==(const RenderEntity&) const = default;
};

/** @brief Map keyed by main-world entity (Bevy `MainEntityHashMap<V>`). */
template <typename V>
using MainEntityHashMap = std::unordered_map<ecs::Entity, V, std::hash<ecs::Entity>>;

/** @brief Set of main-world entities (Bevy `MainEntityHashSet`). */
using MainEntityHashSet = std::unordered_set<ecs::Entity, std::hash<ecs::Entity>>;

/**
 * @brief Marker placed on render-world entities that are despawned at the end
 * of each frame in `RenderSystems::PostCleanup` (Bevy `TemporaryRenderEntity`).
 */
EPIX_EXPORT struct TemporaryRenderEntity {};

/**
 * @brief Main-world resource recording synced components that were removed
 * this frame (Bevy PendingSyncEntity + EntityRecord::ComponentRemoved).
 * Consumed by entity_sync_system to despawn+respawn the render entity,
 * clearing derived/extracted components (sync_world.rs:238-250).
 */
EPIX_EXPORT struct PendingSyncEntity {
    /** @brief Main-world entities whose synced component was removed. */
    std::vector<ecs::Entity> component_removed;
};

/**
 * @brief Synchronizes entities marked with SyncToRenderWorld between the main
 * and render worlds (Bevy `entity_sync_system`). Called at the start of
 * extraction each frame: spawns render-world entities with `MainEntity` for
 * newly-synced main entities (writing `RenderEntity` back), and despawns
 * render entities whose main counterpart is gone or no longer synced.
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
        return std::hash<::epix::ecs::Entity>{}(e.entity);
    }
};

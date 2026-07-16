#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#include <memory>
#endif

#include <epix/ecs/entities.hpp>
#include <epix/ecs/tick.hpp>
#include <epix/ecs/component.hpp>

namespace epix::ecs {
/** @brief Forward declaration of the ECS world. */
EPIX_EXPORT struct World;
/** @brief Forward declaration of the deferred world.
 *  Provides non-architectural change access to world data and
 *  deferred command submission without direct mutation. */
EPIX_EXPORT struct DeferredWorld;

/** @brief Unique identifier for a World instance. */
EPIX_EXPORT struct WorldId : ::epix::utils::int_base<std::uint64_t> {
    using int_base::int_base;
};

EPIX_EXPORT struct Components;
EPIX_EXPORT struct Archetypes;
EPIX_EXPORT struct Storage;

namespace internal {
struct Bundles;
struct CommandQueue;
WorldId world_id(const World& world) noexcept;
const Entities& world_entities(const World& world) noexcept;
Entities& world_entities_mut(World& world) noexcept;
const Storage& world_storage(const World& world) noexcept;
Storage& world_storage_mut(World& world) noexcept;
const Components& world_components(const World& world) noexcept;
Components& world_components_mut(World& world) noexcept;
ComponentsRegistrator world_registrator(World& world) noexcept;
ComponentsQueuedRegistrator world_queued_registrator(World& world) noexcept;
const Archetypes& world_archetypes(const World& world) noexcept;
Archetypes& world_archetypes_mut(World& world) noexcept;
const Bundles& world_bundles(const World& world) noexcept;
Bundles& world_bundles_mut(World& world) noexcept;
CommandQueue& world_command_queue(World& world) noexcept;
Tick world_change_tick(const World& world) noexcept;
Tick world_increment_change_tick(World& world) noexcept;
Tick world_last_change_tick(const World& world) noexcept;
void world_flush_entities(World& world);
void world_flush_commands(World& world);
void world_flush(World& world);

WorldId world_id(const DeferredWorld& world) noexcept;
const Entities& world_entities(const DeferredWorld& world) noexcept;
const Storage& world_storage(const DeferredWorld& world) noexcept;
const Components& world_components(const DeferredWorld& world) noexcept;
ComponentsQueuedRegistrator world_queued_registrator(const DeferredWorld& world) noexcept;
const Archetypes& world_archetypes(const DeferredWorld& world) noexcept;
const Bundles& world_bundles(const DeferredWorld& world) noexcept;
CommandQueue& world_command_queue(DeferredWorld& world) noexcept;
Tick world_change_tick(const DeferredWorld& world) noexcept;
Tick world_last_change_tick(const DeferredWorld& world) noexcept;
}  // namespace internal
}  // namespace epix::ecs
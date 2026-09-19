#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/utils.hpp>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#endif

#include <epix/ecs/core/tick.hpp>
#include <epix/ecs/core/type_id.hpp>
#include <epix/ecs/entity/entities.hpp>
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
EPIX_EXPORT struct ComponentsRegistrator;
EPIX_EXPORT struct ComponentsQueuedRegistrator;
EPIX_EXPORT struct Archetypes;
EPIX_EXPORT struct Storage;
EPIX_EXPORT struct RemovedComponentEvents;

namespace internal {
/** @brief Report a missing resource by name (Bevy's `World::resource` panic).
 *
 * A resource lookup that fails has to say *which* resource is missing: the
 * alternative, `std::optional::value()`, surfaces as a bare "Bad optional
 * access" from wherever the lookup happened to run — an exclusive system, a
 * deferred command, or a command-buffer generation task on the task pool — and
 * nothing in the message identifies the resource or the caller.
 */
template <typename T>
[[noreturn]] void missing_resource() {
    throw std::runtime_error(std::format(
        "Requested resource {} does not exist in the `World`. Did you forget to add it using "
        "`app.insert_resource` / `app.init_resource`? Resources are also implicitly added by plugins.",
        meta::type_id<T>().name()));
}

struct Bundles;
struct CommandQueue;
WorldId world_id(const World& world) noexcept;
const Entities& world_entities(const World& world) noexcept;
Entities& world_entities_mut(World& world) noexcept;
const Storage& world_storage(const World& world) noexcept;
Storage& world_storage_mut(World& world) noexcept;
std::optional<Entity> world_resource_entity(const World& world, TypeId resource_id) noexcept;
void world_reconcile_resource_entity(World& world, Entity entity);
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
const RemovedComponentEvents& world_removed_components(const World& world) noexcept;
RemovedComponentEvents& world_removed_components_mut(World& world) noexcept;
void world_flush_components(World& world);
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

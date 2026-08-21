#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#endif

namespace epix::render {
struct RenderT {
    static ecs::Schedule render_schedule();
};
/** @brief Schedule sentinel for the render sub-app. Use `Render` to refer
 * to the render sub-app and its schedule. */
EPIX_EXPORT inline constexpr RenderT Render;

/**
 * @brief Render schedule system sets (Bevy 0.18 `RenderSystems`).
 *
 * Main chain: ExtractCommands -> PrepareMeshes -> ManageViews -> Queue -> PhaseSort
 * -> Prepare -> Render -> Cleanup -> PostCleanup
 * ExtractCommands -> PrepareAssets -> PrepareMeshes -> Prepare
 * Queue: (QueueMeshes -> QueueSweep)
 * Prepare: (PrepareResources -> PrepareResourcesCollectPhaseBuffers
 *           -> PrepareResourcesFlush -> PrepareBindGroups)
 *
 */
EPIX_EXPORT enum class RenderSystems {
    /** @brief Applies the deferred commands queued during the extract schedule. */
    ExtractCommands,
    /** @brief Prepare assets that have been created/modified/removed this frame. */
    PrepareAssets,
    /** @brief Prepares extracted meshes. */
    PrepareMeshes,
    /** @brief Create any additional views such as those used for shadow mapping. */
    ManageViews,
    /** @brief Queue drawable entities as phase items in render phases. */
    Queue,
    /** @brief Sub-set of Queue where mesh entity queue systems run. */
    QueueMeshes,
    /** @brief Sub-set of Queue where meshes that became invisible/changed phase are removed. */
    QueueSweep,
    /** @brief Sort the sorted render phases and bin keys. */
    PhaseSort,
    /** @brief Prepare render resources from extracted data, create bind groups. */
    Prepare,
    /** @brief Sub-set of Prepare for initializing buffers, textures and uniforms. */
    PrepareResources,
    /** @brief Collect phase buffers after PrepareResources. */
    PrepareResourcesCollectPhaseBuffers,
    /** @brief Flush buffers after PrepareResources, before PrepareBindGroups. */
    PrepareResourcesFlush,
    /** @brief Sub-set of Prepare for constructing bind groups. */
    PrepareBindGroups,
    /** @brief Actual rendering happens here. */
    Render,
    /** @brief Cleanup render resources here. */
    Cleanup,
    /** @brief Final cleanup: entities with TemporaryRenderEntity are despawned. */
    PostCleanup,
};

/** @brief The startup schedule of the render app (Bevy 0.18 `RenderStartup`). */
EPIX_EXPORT inline struct RenderStartupT {
} RenderStartup;

}  // namespace epix::render

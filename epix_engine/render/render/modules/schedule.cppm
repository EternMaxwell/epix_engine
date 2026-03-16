export module epix.render:schedule;

import epix.core;

namespace render {
struct RenderT {
    static core::Schedule render_schedule();
};
/** @brief Schedule sentinel for the render sub-app. Use `Render` to refer
 * to the render sub-app and its schedule. */
export constexpr RenderT Render;

/**
 * @brief Render schedule system sets.
 *
 * PostExtract -> ManageViews -> Queue -> PhaseSort -> Prepare -> Render -> Cleanup
 * PostExtract -> PrepareAssets -> Prepare
 * Prepare: (PrepareResources -> PrepareFlush -> PrepareSets)
 */
export enum class RenderSet {
    /** @brief Runs immediately after extraction from the main world. */
    PostExtract,
    /** @brief Prepare render assets (meshes, textures, etc.). */
    PrepareAssets,
    /** @brief Update and manage camera views. */
    ManageViews,
    /** @brief Queue draw calls and phase items. */
    Queue,
    /** @brief Sort phase items for correct draw order. */
    PhaseSort,
    /** @brief Top-level prepare stage (contains sub-sets below). */
    Prepare,
    /** @brief Prepare GPU resources (buffers, textures). */
    PrepareResources,
    /** @brief Flush pending resource uploads. */
    PrepareFlush,
    /** @brief Prepare bind groups and pipeline layouts. */
    PrepareSets,
    /** @brief Execute the render graph. */
    Render,
    /** @brief Post-render cleanup of temporary resources. */
    Cleanup,
};
}  // namespace render
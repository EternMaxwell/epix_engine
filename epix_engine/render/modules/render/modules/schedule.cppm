export module epix.render:schedule;

import epix.core;

namespace render {
struct RenderT {
    static core::Schedule render_schedule();
};
export constexpr RenderT Render;

/**
 * @brief Render schedule system sets.
 *
 * PostExtract -> ManageViews -> Queue -> PhaseSort -> Prepare -> Render -> Cleanup
 * PostExtract -> PrepareAssets -> Prepare
 * Prepare: (PrepareResources -> PrepareFlush -> PrepareSets)
 */
export enum class RenderSet {
    PostExtract,
    PrepareAssets,
    ManageViews,
    Queue,
    PhaseSort,
    Prepare,
    PrepareResources,
    PrepareFlush,
    PrepareSets,
    Render,
    Cleanup,
};
}  // namespace render
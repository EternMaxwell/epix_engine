#pragma once

#include <epix/core.hpp>

namespace epix::render {
struct RenderT {
    static Schedule render_schedule();
};
inline RenderT Render;

/**
 * @brief Render schedule system sets.
 *
 * PostExtract -> ManageViews -> Queue -> PhaseSort -> Prepare -> Render -> Cleanup
 * PostExtract -> PrepareAssets -> Prepare
 * Prepare: (PrepareResources -> PrepareFlush -> PrepareSets)
 */
enum class RenderSet {
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
}
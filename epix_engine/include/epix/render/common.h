#pragma once

#include <epix/app.h>
#include <epix/common.h>

namespace epix::render {
inline struct ExtractScheduleT {
} ExtractSchedule;
struct RenderT {
    EPIX_API static Schedule render_schedule();
};
inline RenderT Render;

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
}  // namespace epix::render
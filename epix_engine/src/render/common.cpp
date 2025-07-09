#include "epix/render/common.h"

EPIX_API epix::Schedule epix::render::RenderT::render_schedule() {
    Schedule schedule(Render);
    schedule.configure_sets(sets(RenderSet::PostExtract, RenderSet::ManageViews,
                                 RenderSet::Queue, RenderSet::PhaseSort,
                                 RenderSet::Prepare, RenderSet::Render,
                                 RenderSet::Cleanup)
                                .chain());
    schedule.configure_sets(sets(RenderSet::PostExtract,
                                 RenderSet::PrepareAssets, RenderSet::Prepare)
                                .chain());
    schedule.configure_sets(sets(RenderSet::PrepareResources,
                                 RenderSet::PrepareFlush,
                                 RenderSet::PrepareSets)
                                .chain()
                                .in_set(RenderSet::Prepare));
    return schedule;
}
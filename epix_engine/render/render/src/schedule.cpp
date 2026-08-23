#include <epix/render.hpp>
#include <epix/render/schedule.hpp>

using namespace epix::ecs;
using namespace epix::app;

namespace epix::render {
Schedule RenderT::render_schedule() {
    Schedule schedule(Render);
    // Bevy Render::base_schedule (lib.rs:208-248):
    // (1) main chain includes PrepareMeshes and PostCleanup.
    schedule.configure_sets(sets(RenderSystems::ExtractCommands, RenderSystems::PrepareMeshes,
                                 RenderSystems::ManageViews, RenderSystems::Queue, RenderSystems::PhaseSort,
                                 RenderSystems::Prepare, RenderSystems::Render, RenderSystems::Cleanup,
                                 RenderSystems::PostCleanup)
                                .chain());
    // (2) asset chain: ExtractCommands -> PrepareAssets -> PrepareMeshes -> Prepare.
    schedule.configure_sets(sets(RenderSystems::ExtractCommands, RenderSystems::PrepareAssets,
                                 RenderSystems::PrepareMeshes, RenderSystems::Prepare)
                                .chain());
    // (3) mesh queue sub-chain inside Queue, after asset preparation.
    schedule.configure_sets(sets(RenderSystems::QueueMeshes, RenderSystems::QueueSweep)
                                .chain()
                                .in_set(RenderSystems::Queue)
                                .after(RenderSystems::PrepareAssets));
    // (4) prepare sub-chain: PrepareResources -> PrepareResourcesCollectPhaseBuffers -> PrepareResourcesFlush ->
    // PrepareBindGroups.
    schedule.configure_sets(sets(RenderSystems::PrepareResources, RenderSystems::PrepareResourcesCollectPhaseBuffers,
                                 RenderSystems::PrepareResourcesFlush, RenderSystems::PrepareBindGroups)
                                .chain()
                                .in_set(RenderSystems::Prepare));
    auto config = schedule.default_schedule_config();
    config.executor_config.deferred =
        DeferredApply::ApplyDirect;  // commands are applied immediately after system execution
    schedule.set_default_schedule_config(config);
    return schedule;
}
}  // namespace epix::render

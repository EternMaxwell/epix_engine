module epix.render;
import :schedule;

using namespace epix::core;

namespace epix::render {
Schedule RenderT::render_schedule() {
    Schedule schedule(Render);
    schedule.configure_sets(sets(RenderSet::PostExtract, RenderSet::ManageViews, RenderSet::Queue, RenderSet::PhaseSort,
                                 RenderSet::Prepare, RenderSet::Render, RenderSet::Cleanup)
                                .chain());
    schedule.configure_sets(sets(RenderSet::PostExtract, RenderSet::PrepareAssets, RenderSet::Prepare).chain());
    schedule.configure_sets(sets(RenderSet::PrepareResources, RenderSet::PrepareFlush, RenderSet::PrepareSets)
                                .chain()
                                .in_set(RenderSet::Prepare));
    auto config = schedule.default_schedule_config();
    config.executor_config.deferred =
        DeferredApply::ApplyDirect;  // commands are applied immediately after system execution
    schedule.set_default_schedule_config(config);
    return schedule;
}
}  // namespace render

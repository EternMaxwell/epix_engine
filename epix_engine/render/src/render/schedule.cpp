#include "epix/core.hpp"
#include "epix/render/schedule.hpp"

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
    auto config         = schedule.default_execute_config();
    config.apply_direct = true;  // apply direct set to true so commands are applied immediately after system execution
    schedule.set_default_execute_config(config);
    return std::move(schedule);
}
}  // namespace epix::render
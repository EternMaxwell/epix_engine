#include <spdlog/spdlog.h>

#include <epix/render.hpp>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

void FrameCountPlugin::attach(App& app) {
    spdlog::debug("[render.globals] Attaching FrameCountPlugin.");
    app.world_mut().init_resource<FrameCount>();
    // Bevy increments FrameCount in the Last schedule (0 during the first update,
    // 1 during the next), see bevy_diagnostic::FrameCountPlugin.
    app.add_systems(Last, into(detail::increment_frame_count).set_name("increment frame count"));
}

void GlobalsPlugin::attach(App& app) {
    spdlog::debug("[render.globals] Attaching GlobalsPlugin.");
    auto render_app = app.get_sub_app_mut(epix::render::Render);
    if (!render_app) return;
    // Render-world copies of Time and FrameCount, populated by the extract
    // systems below (Bevy: init_resource::<GlobalsBuffer>().init_resource::<Time>()).
    auto& render_world = render_app->get();
    render_world.world_mut().init_resource<GlobalsBuffer>();
    render_world.world_mut().init_resource<epix::time::Time<>>();
    render_world.add_systems(ExtractSchedule, into(detail::extract_frame_count, detail::extract_time)
                                                  .set_names(std::array{"extract frame count", "extract time"}));
    render_world.add_systems(epix::render::Render, into(detail::prepare_globals_buffer)
                                                       .in_set(epix::render::RenderSystems::PrepareResources)
                                                       .set_name("prepare globals buffer"));
}

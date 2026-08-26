#include <spdlog/spdlog.h>

#include <epix/render.hpp>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;

void view::RenderVisibilityRangePlugin::attach(App& app) {
    spdlog::debug("[render.view] Attaching RenderVisibilityRangePlugin.");
    auto render_app = app.get_sub_app_mut(Render);
    if (!render_app) return;
    auto& render_world = render_app->get();
    render_world.world_mut().init_resource<view::RenderVisibilityRanges>();
    render_world.add_systems(ExtractSchedule,
                             into(view::extract_visibility_ranges).set_name("extract visibility ranges"));
    render_world.add_systems(epix::render::Render, into(view::detail::write_render_visibility_ranges)
                                                       .in_set(epix::render::RenderSystems::PrepareResourcesFlush)
                                                       .set_name("write render visibility ranges"));
}

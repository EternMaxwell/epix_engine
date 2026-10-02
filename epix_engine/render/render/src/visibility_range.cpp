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

void view::RenderVisibilityRanges::insert(sync_world::MainEntity entity,
                                          const ::epix::camera::VisibilityRange& visibility_range) {
    std::uint16_t buffer_index = 0;
    if (auto it = range_to_index.find(visibility_range); it != range_to_index.end()) {
        buffer_index = it->second;
    } else {
        // Indices are assigned from range_to_index.size() and never reused
        // (Bevy range.rs:115-146); the buffer slot at that index holds the
        // range vec4 and is stable for the app lifetime.
        if (range_to_index.size() >= std::numeric_limits<std::uint16_t>::max()) {
            spdlog::warn("[render] Too many distinct visibility ranges; index truncated.");
        }
        buffer_index = static_cast<std::uint16_t>(range_to_index.size());
        buffer.push(glm::vec4(visibility_range.start_margin_start, visibility_range.start_margin_end,
                              visibility_range.end_margin_start, visibility_range.end_margin_end));
        range_to_index.emplace(visibility_range, buffer_index);
        buffer_dirty = true;
    }
    entities.emplace(entity, detail::RenderVisibilityEntityInfo{buffer_index, visibility_range.is_abrupt()});
}


#include <spdlog/spdlog.h>

#include <epix/core_graph.hpp>
#include <optional>
#include <unordered_set>
using namespace epix::render;
using namespace epix::core_graph::core_2d;
using namespace epix::ecs;
using namespace epix::app;

namespace {
void extract_core2d_camera_phases(
    ::epix::ecs::ResMut<::epix::render::phase::ViewSortedRenderPhases<::epix::core_graph::core_2d::Transparent2D>>
        transparent_phases,
    ::epix::ecs::ResMut<::epix::render::phase::ViewSortedRenderPhases<::epix::core_graph::core_2d::Opaque2D>>
        opaque_phases,
    ::epix::app::Extract<::epix::ecs::Query<::epix::ecs::Item<::epix::ecs::Entity,
                                                               const ::epix::camera::Camera&,
                                                               const ::epix::render::camera::CameraRenderGraph&>,
                                            ::epix::ecs::With<::epix::camera::Camera2d>>>
                cameras) {
    std::unordered_set<::epix::render::view::RetainedViewEntity> live_views;
    for (auto&& [entity, camera, graph] : cameras.iter()) {
        if (!camera.is_active ||
            graph != ::epix::render::graph::GraphLabel(::epix::core_graph::core_2d::Core2d))
            continue;
        const auto retained_view = ::epix::render::view::RetainedViewEntity::create(
            ::epix::render::sync_world::MainEntity{entity}, std::nullopt, 0);
        transparent_phases->insert_or_clear(retained_view);
        opaque_phases->insert_or_clear(retained_view);
        live_views.insert(retained_view);
    }
    const auto remove_dead_views = [&live_views](const auto& entry) { return !live_views.contains(entry.first); };
    std::erase_if(*transparent_phases, remove_dead_views);
    std::erase_if(*opaque_phases, remove_dead_views);
}
}  // namespace

void Core2dGraph::add_to(graph::RenderGraph& g, World& world) {
    spdlog::debug("[render.core_graph] Adding Core2D sub-graph to render graph.");
    graph::RenderGraph g2d;
    g2d.add_node(Core2dNodes::StartMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::MainTransparentPass, Node2D<Transparent2D>{});
    g2d.add_node(Core2dNodes::MainOpaquePass, Node2D<Opaque2D>{});
    g2d.add_node(Core2dNodes::EndMainPass, graph::EmptyNode{});
    // These endpoints are intentionally installed by Core2dPlugin: Bevy does
    // the same so optional post-processing plugins can insert work between
    // them. Tonemapping itself remains a no-op placeholder until C16 supplies
    // its Bevy-shaped GPU implementation.
    g2d.add_node(Core2dNodes::StartMainPassPostProcessing, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::Tonemapping, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::EndMainPassPostProcessing, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::Upscaling, graph::ViewNodeRunner{UpscalingNode{}, world});
    g2d.add_node_edges(Core2dNodes::StartMainPass, Core2dNodes::MainOpaquePass, Core2dNodes::MainTransparentPass,
                       Core2dNodes::EndMainPass, Core2dNodes::StartMainPassPostProcessing,
                       Core2dNodes::Tonemapping, Core2dNodes::EndMainPassPostProcessing, Core2dNodes::Upscaling);
    g.add_sub_graph(Core2d, std::move(g2d));
}

void Core2dPlugin::attach(App& app) {
    // Mirror Core2dPlugin::build: Camera2d gets the Core2D graph plus the
    // component defaults that define a plain 2D camera output path.
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return DebandDither::Disabled; });
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return render::camera::CameraRenderGraph{Core2d}; });
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return Tonemapping::None; });
    app.add_plugins(render::ExtractComponentPlugin<::epix::camera::Camera2d>{});
    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().init_resource<phase::DrawFunctions<Transparent2D>>();
        render_app.world_mut().init_resource<phase::DrawFunctions<Opaque2D>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transparent2D>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Opaque2D>>();
        Core2d.add_to(render_app.resource_mut<graph::RenderGraph>(), render_app.world_mut());

        render_app.add_systems(Render, into(phase::sort_phase_system<Transparent2D>, phase::sort_phase_system<Opaque2D>)
                                           .in_set(RenderSystems::PhaseSort)
                                           .set_names(std::array{"sort transparent 2d phase", "sort opaque 2d phase"}));
        render_app.add_systems(ExtractSchedule,
                               into(extract_core2d_camera_phases).set_name("extract core 2d camera phases"));
        return std::make_optional(std::ref(render_app));
    });
}

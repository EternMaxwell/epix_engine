module;

#include <spdlog/spdlog.h>

module epix.core_graph;

import std;

using namespace epix::render;
using namespace epix::core_graph::core_2d;

void Core2dGraph::add_to(graph::RenderGraph& g) {
    spdlog::debug("[render.core_graph] Adding Core2D sub-graph to render graph.");
    graph::RenderGraph g2d;
    g2d.add_node(Core2dNodes::StartMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::MainTransparentPass, Node2D<Transparent2D>{});
    g2d.add_node(Core2dNodes::MainOpaquePass, Node2D<Opaque2D>{});
    g2d.add_node(Core2dNodes::EndMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::ScreenUIPass, Node2D<UI2DItem>{});
    g2d.add_node_edges(Core2dNodes::StartMainPass, Core2dNodes::MainOpaquePass, Core2dNodes::MainTransparentPass,
                       Core2dNodes::EndMainPass, Core2dNodes::ScreenUIPass);
    auto res = g.add_sub_graph(Core2d, std::move(g2d)).transform_error([](auto&& err) {
        spdlog::error("Failed to add Core2D graph: {}", err.to_string());
        return std::move(err);
    });
}

void Core2dPlugin::build(App& app) {
    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().insert_resource(phase::DrawFunctions<Transparent2D>{});
        render_app.world_mut().insert_resource(phase::DrawFunctions<Opaque2D>{});
        render_app.world_mut().insert_resource(phase::DrawFunctions<UI2DItem>{});
        Core2d.add_to(render_app.resource_mut<graph::RenderGraph>());

        render_app.add_systems(Render, into(phase::sort_phase_items<Transparent2D>, phase::sort_phase_items<UI2DItem>,
                                            phase::sort_phase_items<Opaque2D>)
                                           .in_set(RenderSet::PhaseSort)
                                           .set_names(std::array{"sort transparent 2d phase", "sort ui 2d phase",
                                                                 "sort opaque 2d phase"}));
        render_app.add_systems(
            Render, into([](Commands cmd,
                            Query<Item<Entity, const camera::ExtractedCamera&>, With<view::ExtractedView>> views) {
                        // insert render phases for each view
                        for (auto&& [entity, camera] : views.iter()) {
                            // only insert for 2d camera render graph
                            if (camera.render_graph == camera::CameraRenderGraph(Core2d)) {
                                auto entity_commands = cmd.entity(entity);
                                entity_commands.insert(phase::RenderPhase<Transparent2D>{},
                                                       phase::RenderPhase<Opaque2D>{}, phase::RenderPhase<UI2DItem>{});
                            }
                        }
                    })
                        .in_set(RenderSet::ManageViews)
                        .set_name("insert 2d render phases"));
        return std::make_optional(std::ref(render_app));
    });
}

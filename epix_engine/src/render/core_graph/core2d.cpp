#include <epix/render/core_graph/core_2d.h>

using namespace epix;
using namespace epix::render;
using namespace epix::render::core_2d;

EPIX_API void Core2dGraph::add_to(graph::RenderGraph& g) {
    graph::RenderGraph g2d;
    g2d.add_node(Core2dNodes::StartMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::MainTransparentPass, Node2D<Transparent2D>{});
    g2d.add_node(Core2dNodes::MainOpaquePass, Node2D<Opaque2D>{});
    g2d.add_node(Core2dNodes::EndMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::ScreenUIPass, Node2D<UI2DItem>{});
    g2d.add_node_edges(Core2dNodes::StartMainPass, Core2dNodes::MainOpaquePass, Core2dNodes::MainTransparentPass,
                       Core2dNodes::EndMainPass, Core2dNodes::ScreenUIPass);
    g.add_sub_graph(Core2d, std::move(g2d));
}

EPIX_API void Core2dPlugin::build(App& app) {
    if (auto render_app = app.get_sub_app(render::Render)) {
        render_app->insert_resource(render_phase::DrawFunctions<Transparent2D>{});
        render_app->insert_resource(render_phase::DrawFunctions<Opaque2D>{});
        render_app->insert_resource(render_phase::DrawFunctions<UI2DItem>{});
        Core2d.add_to(render_app->resource<graph::RenderGraph>());

        render_app->add_systems(
            Render, into(render_phase::sort_phase_items<Transparent2D>, render_phase::sort_phase_items<UI2DItem>)
                        .in_set(RenderSet::PhaseSort)
                        .set_names({"sort transparent 2d phase", "sort ui 2d phase"}));
    }
}
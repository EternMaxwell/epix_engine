#pragma once

#include "../camera.h"
#include "../common.h"
#include "../graph.h"
#include "../pipeline.h"
#include "../render_phase.h"
#include "../view.h"

namespace epix::render::core_2d {

enum class Core2dNodes {
    StartMainPass,
    MainTransparentPass,
    MainOpaquePass,
    EndMainPass,
    ScreenUIPass,
};

/**
 * @brief A transparent 2D render phase item.
 *
 * The rendered object might be transparent, so this is preferred to be sorted before rendering.
 */
struct Transparent2D {
    Entity id;
    float depth;
    RenderPipelineId pipeline_id;
    render_phase::DrawFunctionId draw_func;
    size_t batch_count;

    Entity entity() const { return id; }
    float sort_key() const { return -depth; }  // inverse depth for back-to-front rendering
    render_phase::DrawFunctionId draw_function() const { return draw_func; }
    RenderPipelineId pipeline() const { return pipeline_id; }

    size_t batch_size() const { return batch_count; }
};
static_assert(render_phase::BatchedPhaseItem<Transparent2D>);
static_assert(render_phase::CachedRenderPipelinePhaseItem<Transparent2D>);

struct Opaque2D {
    Entity id;
    RenderPipelineId pipeline_id;
    render_phase::DrawFunctionId draw_func;
    size_t batch_count;

    Entity entity() const { return id; }
    float sort_key() const { return 0.0f; }  // just return 0 since there is no need to sort opaque items by z order
    render_phase::DrawFunctionId draw_function() const { return draw_func; }
    RenderPipelineId pipeline() const { return pipeline_id; }
    size_t batch_size() const { return batch_count; }
};
static_assert(render_phase::BatchedPhaseItem<Opaque2D>);
static_assert(render_phase::CachedRenderPipelinePhaseItem<Opaque2D>);

struct UI2DItem {
    Entity id;
    int order;  // UI elements with higher order are rendered on top of those with lower order
    RenderPipelineId pipeline_id;
    render_phase::DrawFunctionId draw_func;
    size_t batch_count;

    Entity entity() const { return id; }
    int sort_key() const { return order; }
    render_phase::DrawFunctionId draw_function() const { return draw_func; }
    RenderPipelineId pipeline() const { return pipeline_id; }
    size_t batch_size() const { return batch_count; }
};

template <typename P>
struct Node2D : graph::Node {
    std::optional<Query<Item<view::ExtractedView, view::ViewTarget, view::ViewDepth, render_phase::RenderPhase<P>>>>
        views;
    void update(const World& world) override { views.emplace(world); }
    void run(graph::GraphContext& ctx, graph::RenderContext& render_ctx, const World& world) override {
        auto view_entity = ctx.view_entity();
        auto view_opt    = views->try_get(view_entity);
        if (!view_opt) return;
        auto& [exview, target, depth, phase] = *view_opt;
        nvrhi::FramebufferHandle framebuffer = render_ctx.device()->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(target.texture).setDepthAttachment(depth.texture));
        nvrhi::GraphicsState state = nvrhi::GraphicsState()
                                         .setFramebuffer(framebuffer)
                                         .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(
                                             exview.viewport_origin.x, exview.viewport_origin.y,
                                             exview.viewport_origin.x + exview.viewport_size.x,
                                             exview.viewport_origin.y + exview.viewport_size.y, 0.f, 1.f)));
        render_phase::DrawContext cmd{render_ctx.commands(), state};
        phase.render(cmd, world, view_entity);
        render_ctx.flush_encoder();
    }
};

inline struct Core2dGraph {
    void add_to(graph::RenderGraph& g);
} Core2d;
inline void Core2dGraph::add_to(graph::RenderGraph& g) {
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

struct Core2dPlugin {
    EPIX_API void build(App& app) {
        if (auto render_app = app.get_sub_app(render::Render)) {
            render_app->insert_resource<render_phase::DrawFunctions<Transparent2D>>();
            render_app->insert_resource<render_phase::DrawFunctions<Opaque2D>>();
            render_app->insert_resource<render_phase::DrawFunctions<UI2DItem>>();
            Core2d.add_to(render_app->resource<graph::RenderGraph>());

            render_app->add_systems(
                Render, into(render_phase::sort_phase_items<Transparent2D>, render_phase::sort_phase_items<UI2DItem>)
                            .in_set(RenderSet::PhaseSort)
                            .set_names({"sort transparent 2d phase", "sort ui 2d phase"}));
        }
    }
};

struct Camera2D {};

struct Camera2DBundle : public Bundle {
    render::camera::Camera camera;
    render::camera::Projection projection;
    render::camera::CameraRenderGraph render_graph = Core2d;
    transform::Transform transform;
    view::VisibleEntities visible_entities;
    Camera2D camera_2d;

    std::tuple<camera::Camera,
               camera::Projection,
               camera::CameraRenderGraph,
               transform::Transform,
               view::VisibleEntities,
               Camera2D>
    unpack() {
        return {camera, projection, render_graph, transform, visible_entities, camera_2d};
    }
};
}  // namespace epix::render::core_2d
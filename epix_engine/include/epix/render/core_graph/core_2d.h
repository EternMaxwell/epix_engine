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

struct TransparentNode : graph::Node {
    std::optional<Query<
        Item<view::ExtractedView, view::ViewTarget, view::ViewDepth, Mut<render_phase::RenderPhase<Transparent2D>>>>>
        views;
    void update(World& world) override { views.emplace(world); }
    void run(graph::GraphContext& ctx, graph::RenderContext& render_ctx, World& world) override {
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
struct OpaqueNode : graph::Node {
    std::optional<
        Query<Item<view::ExtractedView, view::ViewTarget, view::ViewDepth, Mut<render_phase::RenderPhase<Opaque2D>>>>>
        views;
    void update(World& world) override { views.emplace(world); }
    void run(graph::GraphContext& ctx, graph::RenderContext& render_ctx, World& world) override {
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
void Core2dGraph::add_to(graph::RenderGraph& g) {
    graph::RenderGraph g2d;
    g2d.add_node(Core2dNodes::StartMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::MainTransparentPass, TransparentNode{});
    g2d.add_node(Core2dNodes::MainOpaquePass, OpaqueNode{});
    g2d.add_node(Core2dNodes::EndMainPass, graph::EmptyNode{});
    g2d.add_node_edges(Core2dNodes::StartMainPass, Core2dNodes::MainOpaquePass, Core2dNodes::MainTransparentPass,
                       Core2dNodes::EndMainPass);
    g.add_sub_graph(Core2d, std::move(g2d));
}
}  // namespace epix::render::core_2d
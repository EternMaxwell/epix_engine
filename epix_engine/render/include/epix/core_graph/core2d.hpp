#pragma once

#include <epix/render.hpp>

#include "epix/core/type_system/type_registry.hpp"

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
    std::optional<QueryState<Item<const view::ExtractedView&,
                                  const view::ViewTarget&,
                                  const view::ViewDepth&,
                                  const render_phase::RenderPhase<P>&>,
                             Filter<>>>
        views;
    void update(World& world) override {
        if (!views) {
            views = world.query<Item<const view::ExtractedView&, const view::ViewTarget&, const view::ViewDepth&,
                                     const render_phase::RenderPhase<P>&>>();
        } else {
            views->update_archetypes(world);
        }
    }
    void run(graph::GraphContext& ctx, graph::RenderContext& render_ctx, World& world) override {
        auto view_entity = ctx.view_entity();
        auto view_opt = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view_entity);
        if (!view_opt) return;
        auto&& [exview, target, depth, phase] = *view_opt;
        nvrhi::FramebufferHandle framebuffer  = render_ctx.device()->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(target.texture).setDepthAttachment(depth.texture));
        nvrhi::GraphicsState state =
            nvrhi::GraphicsState()
                .setFramebuffer(framebuffer)
                .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(nvrhi::Viewport(
                    exview.viewport_origin.x, exview.viewport_origin.x + exview.viewport_size.x,
                    exview.viewport_origin.y, exview.viewport_origin.y + exview.viewport_size.y, 0.f, 1.f)));
        render_phase::DrawContext cmd{render_ctx.commands(), state};
        phase.render(cmd, world, view_entity);
        render_ctx.flush_encoder();
    }
};

inline struct Core2dGraph {
    void add_to(graph::RenderGraph& g);
} Core2d;

struct Core2dPlugin {
    void build(App& app);
};

struct Camera2D {};

struct Camera2DBundle {
    render::camera::Camera camera;
    render::camera::Projection projection;
    render::camera::CameraRenderGraph render_graph = Core2d;
    transform::Transform transform;
    view::VisibleEntities visible_entities;
    Camera2D camera_2d;
};
}  // namespace epix::render::core_2d

template <>
struct epix::core::bundle::Bundle<epix::render::core_2d::Camera2DBundle> {
    static size_t write(epix::render::core_2d::Camera2DBundle& bundle, std::span<void*> dest) {
        new (dest[0]) render::camera::Camera(std::move(bundle.camera));
        new (dest[1]) render::camera::Projection(std::move(bundle.projection));
        new (dest[2]) render::camera::CameraRenderGraph(std::move(bundle.render_graph));
        new (dest[3]) transform::Transform(std::move(bundle.transform));
        new (dest[4]) render::view::VisibleEntities(std::move(bundle.visible_entities));
        new (dest[5]) render::core_2d::Camera2D(std::move(bundle.camera_2d));
        return 6;
    }
    static std::array<epix::TypeId, 6> type_ids(const epix::core::type_system::TypeRegistry& registry) {
        return std::array{
            registry.type_id<render::camera::Camera>(),
            registry.type_id<render::camera::Projection>(),
            registry.type_id<render::camera::CameraRenderGraph>(),
            registry.type_id<transform::Transform>(),
            registry.type_id<render::view::VisibleEntities>(),
            registry.type_id<render::core_2d::Camera2D>(),
        };
    }
    static void register_components(const epix::core::type_system::TypeRegistry& registry,
                                    epix::core::Components& components) {
        components.register_info<render::camera::Camera>();
        components.register_info<render::camera::Projection>();
        components.register_info<render::camera::CameraRenderGraph>();
        components.register_info<transform::Transform>();
        components.register_info<render::view::VisibleEntities>();
        components.register_info<render::core_2d::Camera2D>();
    }
};
static_assert(epix::core::bundle::is_bundle<epix::render::core_2d::Camera2DBundle>);
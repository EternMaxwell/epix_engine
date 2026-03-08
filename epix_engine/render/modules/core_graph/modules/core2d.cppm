module;

export module epix.core_graph:core2d;

import epix.core;
import epix.render;
import epix.transform;

import std;

using namespace core;
using namespace render;

namespace core_graph::core_2d {

export enum class Core2dNodes {
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
export struct Transparent2D {
    Entity id;
    float depth;
    CachedPipelineId pipeline_id;
    phase::DrawFunctionId draw_func;
    size_t batch_count;

    Entity entity() const { return id; }
    float sort_key() const { return -depth; }  // inverse depth for back-to-front rendering
    phase::DrawFunctionId draw_function() const { return draw_func; }
    CachedPipelineId pipeline() const { return pipeline_id; }

    size_t batch_size() const { return batch_count; }
};
static_assert(phase::BatchedPhaseItem<Transparent2D>);
static_assert(phase::CachedRenderPipelinePhaseItem<Transparent2D>);

export struct Opaque2D {
    Entity id;
    CachedPipelineId pipeline_id;
    phase::DrawFunctionId draw_func;
    size_t batch_count;
    phase::OpaqueSortKey batch_key;

    Entity entity() const { return id; }
    const phase::OpaqueSortKey& sort_key() const { return batch_key; }
    phase::DrawFunctionId draw_function() const { return draw_func; }
    CachedPipelineId pipeline() const { return pipeline_id; }
    size_t batch_size() const { return batch_count; }
};
static_assert(phase::BatchedPhaseItem<Opaque2D>);
static_assert(phase::CachedRenderPipelinePhaseItem<Opaque2D>);

export struct UI2DItem {
    Entity id;
    int order;  // UI elements with higher order are rendered on top of those with lower order
    CachedPipelineId pipeline_id;
    phase::DrawFunctionId draw_func;
    size_t batch_count;

    Entity entity() const { return id; }
    int sort_key() const { return order; }
    phase::DrawFunctionId draw_function() const { return draw_func; }
    CachedPipelineId pipeline() const { return pipeline_id; }
    size_t batch_size() const { return batch_count; }
};

template <typename P>
struct Node2D : graph::Node {
    std::optional<QueryState<
        Item<const view::ExtractedView&, const view::ViewTarget&, const view::ViewDepth&, const phase::RenderPhase<P>&>,
        Filter<>>>
        views;
    void update(const World& world) override {
        if (!views) {
            views = world.try_query<Item<const view::ExtractedView&, const view::ViewTarget&, const view::ViewDepth&,
                                         const phase::RenderPhase<P>&>>();
        } else {
            views->update_archetypes(world);
        }
    }
    void run(graph::GraphContext& ctx, graph::RenderContext& render_ctx, const World& world) override {
        if (!views) return;  // likely be components of the query not all got registered, just skip running for now
        auto view_entity = ctx.view_entity();
        auto view_opt = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view_entity);
        if (!view_opt) return;
        auto&& [exview, target, depth, phase] = *view_opt;
        auto render_pass                      = render_ctx.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor()
                .setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                    .setView(target.texture_view)
                                                    .setDepthSlice(~0u)
                                                    .setLoadOp(wgpu::LoadOp::eLoad)
                                                    .setStoreOp(wgpu::StoreOp::eStore)})
                .setDepthStencilAttachment(wgpu::RenderPassDepthStencilAttachment()
                                               .setView(depth.depth_view)
                                               .setDepthLoadOp(wgpu::LoadOp::eLoad)
                                               .setDepthStoreOp(wgpu::StoreOp::eStore)));
        phase.render(render_pass, world, view_entity);
        render_pass.end();
        render_ctx.flush_encoder();
    }
};

export inline struct Core2dGraph {
    void add_to(graph::RenderGraph& g);
} Core2d;

export struct Core2dPlugin {
    void build(App& app);
};

export struct Camera2D {};

export struct Camera2DBundle {
    render::camera::Camera camera;
    render::camera::Projection projection;
    render::camera::CameraRenderGraph render_graph = Core2d;
    transform::Transform transform;
    view::VisibleEntities visible_entities;
    Camera2D camera_2d;
};
}  // namespace core_graph::core_2d

template <>
struct core::Bundle<core_graph::core_2d::Camera2DBundle> {
    static size_t write(core_graph::core_2d::Camera2DBundle& bundle, std::span<void*> dest) {
        new (dest[0]) render::camera::Camera(std::move(bundle.camera));
        new (dest[1]) render::camera::Projection(std::move(bundle.projection));
        new (dest[2]) render::camera::CameraRenderGraph(std::move(bundle.render_graph));
        new (dest[3]) transform::Transform(std::move(bundle.transform));
        new (dest[4]) render::view::VisibleEntities(std::move(bundle.visible_entities));
        new (dest[5]) core_graph::core_2d::Camera2D(std::move(bundle.camera_2d));
        return 6;
    }
    static std::array<TypeId, 6> type_ids(const core::TypeRegistry& registry) {
        return std::array{
            registry.type_id<render::camera::Camera>(),
            registry.type_id<render::camera::Projection>(),
            registry.type_id<render::camera::CameraRenderGraph>(),
            registry.type_id<transform::Transform>(),
            registry.type_id<render::view::VisibleEntities>(),
            registry.type_id<core_graph::core_2d::Camera2D>(),
        };
    }
    static void register_components(const core::TypeRegistry& registry, core::Components& components) {
        components.register_info<render::camera::Camera>();
        components.register_info<render::camera::Projection>();
        components.register_info<render::camera::CameraRenderGraph>();
        components.register_info<transform::Transform>();
        components.register_info<render::view::VisibleEntities>();
        components.register_info<core_graph::core_2d::Camera2D>();
    }
};
static_assert(core::is_bundle<core_graph::core_2d::Camera2DBundle>);
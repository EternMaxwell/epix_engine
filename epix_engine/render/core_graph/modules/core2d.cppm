module;

export module epix.core_graph:core2d;

import epix.core;
import epix.render;
import epix.transform;

import std;

using namespace epix::core;
using namespace epix::render;

namespace epix::core_graph::core_2d {

/** @brief Node labels for the 2D render graph passes. */
export enum class Core2dNodes {
    /** @brief Node that begins the main render pass. */
    StartMainPass,
    /** @brief Node for rendering transparent 2D items. */
    MainTransparentPass,
    /** @brief Node for rendering opaque 2D items. */
    MainOpaquePass,
    /** @brief Node that ends the main render pass. */
    EndMainPass,
    /** @brief Node for the screen-space UI pass. */
    ScreenUIPass,
};

/**
 * @brief A transparent 2D render phase item.
 *
 * Sorted by inverse depth for back-to-front rendering. Supports instanced
 * batching.
 */
export struct Transparent2D {
    /** @brief Entity this phase item refers to. */
    Entity id;
    /** @brief Depth value for sorting (inverted for back-to-front). */
    float depth;
    /** @brief Cached render pipeline ID. */
    CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    phase::DrawFunctionId draw_func;
    /** @brief Number of instances in this batch. */
    size_t batch_count;

    Entity entity() const { return id; }
    float sort_key() const { return -depth; }  // inverse depth for back-to-front rendering
    phase::DrawFunctionId draw_function() const { return draw_func; }
    CachedPipelineId pipeline() const { return pipeline_id; }

    size_t batch_size() const { return batch_count; }
};
static_assert(phase::BatchedPhaseItem<Transparent2D>);
static_assert(phase::CachedRenderPipelinePhaseItem<Transparent2D>);

/** @brief An opaque 2D render phase item.
 *
 * Sorted by OpaqueSortKey for front-to-back rendering and batching.
 */
export struct Opaque2D {
    /** @brief Entity this phase item refers to. */
    Entity id;
    /** @brief Cached render pipeline ID. */
    CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    phase::DrawFunctionId draw_func;
    /** @brief Number of instances in this batch. */
    size_t batch_count;
    /** @brief Sort key for front-to-back opaque ordering. */
    phase::OpaqueSortKey batch_key;

    Entity entity() const { return id; }
    const phase::OpaqueSortKey& sort_key() const { return batch_key; }
    phase::DrawFunctionId draw_function() const { return draw_func; }
    CachedPipelineId pipeline() const { return pipeline_id; }
    size_t batch_size() const { return batch_count; }
};
static_assert(phase::BatchedPhaseItem<Opaque2D>);
static_assert(phase::CachedRenderPipelinePhaseItem<Opaque2D>);

/** @brief A UI 2D render phase item.
 *
 * Sorted by `order` for z-ordering of UI elements.
 */
export struct UI2DItem {
    /** @brief Entity this phase item refers to. */
    Entity id;
    /** @brief Z-order for UI stacking (higher = on top). */
    int order;
    /** @brief Cached render pipeline ID. */
    CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    phase::DrawFunctionId draw_func;
    /** @brief Number of instances in this batch. */
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

/** @brief Singleton struct for initializing the core 2D render graph. */
export inline struct Core2dGraph {
    /** @brief Add this graph as a sub-graph to the given render graph. */
    void add_to(graph::RenderGraph& g);
} Core2d;

/** @brief Plugin that sets up the core 2D render graph and camera
 * projection. */
export struct Core2dPlugin {
    void build(App& app);
};

/** @brief Marker component for 2D camera entities. */
export struct Camera2D {};

/** @brief Bundle for spawning a complete 2D camera entity configured with
 * the core 2D render graph. */
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
struct epix::core::Bundle<epix::core_graph::core_2d::Camera2DBundle> {
    static size_t write(epix::core_graph::core_2d::Camera2DBundle& bundle, std::span<void*> dest) {
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
static_assert(epix::core::is_bundle<epix::core_graph::core_2d::Camera2DBundle>);
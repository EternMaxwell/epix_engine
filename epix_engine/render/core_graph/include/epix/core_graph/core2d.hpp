#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::core_graph::core_2d {

/** @brief Node labels for the 2D render graph passes. */
EPIX_EXPORT enum class Core2dNodes {
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
EPIX_EXPORT struct Transparent2D {
    /** @brief Entity this phase item refers to. */
    ecs::Entity id;
    /** @brief Depth value for sorting (inverted for back-to-front). */
    float depth;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Number of instances in this batch. */
    std::size_t batch_count;

    ecs::Entity entity() const noexcept { return id; }
    float sort_key() const noexcept { return -depth; }  // inverse depth for back-to-front rendering
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }

    std::size_t batch_size() const noexcept { return batch_count; }
};
static_assert(render::phase::BatchedPhaseItem<Transparent2D>);
static_assert(render::phase::CachedRenderPipelinePhaseItem<Transparent2D>);

/** @brief An opaque 2D render phase item.
 *
 * Sorted by OpaqueSortKey for front-to-back rendering and batching.
 */
EPIX_EXPORT struct Opaque2D {
    /** @brief Entity this phase item refers to. */
    ecs::Entity id;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Number of instances in this batch. */
    std::size_t batch_count;
    /** @brief Sort key for front-to-back opaque ordering. */
    render::phase::OpaqueSortKey batch_key;

    ecs::Entity entity() const noexcept { return id; }
    const render::phase::OpaqueSortKey& sort_key() const noexcept { return batch_key; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    std::size_t batch_size() const noexcept { return batch_count; }
};
static_assert(render::phase::BatchedPhaseItem<Opaque2D>);
static_assert(render::phase::CachedRenderPipelinePhaseItem<Opaque2D>);

/** @brief A UI 2D render phase item.
 *
 * Sorted by `order` for z-ordering of UI elements.
 */
EPIX_EXPORT struct UI2DItem {
    /** @brief Entity this phase item refers to. */
    ecs::Entity id;
    /** @brief Z-order for UI stacking (higher = on top). */
    int order;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Number of instances in this batch. */
    std::size_t batch_count;

    ecs::Entity entity() const noexcept { return id; }
    int sort_key() const noexcept { return order; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    std::size_t batch_size() const noexcept { return batch_count; }
};

template <typename P>
struct Node2D : render::graph::Node {
    std::optional<ecs::QueryState<ecs::Item<const render::view::ExtractedView&,
                                            const render::view::ViewTarget&,
                                            const render::view::ViewDepth&,
                                            const render::phase::RenderPhase<P>&>,
                                  ecs::Filter<>>>
        views;
    void update(const ecs::World& world) override {
        if (!views) {
            views = world.try_query<ecs::Item<const render::view::ExtractedView&, const render::view::ViewTarget&,
                                              const render::view::ViewDepth&, const render::phase::RenderPhase<P>&>>();
        } else {
            views->update_archetypes(world);
        }
    }
    void run(render::graph::GraphContext& ctx,
             render::graph::RenderContext& render_ctx,
             const ecs::World& world) override {
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
EPIX_EXPORT inline struct Core2dGraph {
    /** @brief Add this graph as a sub-graph to the given render graph. */
    void add_to(render::graph::RenderGraph& g);
} Core2d;

/** @brief Plugin that sets up the core 2D render graph and camera
 * projection. */
EPIX_EXPORT struct Core2dPlugin {
    void attach(app::App& app);
};

/** @brief Marker component for 2D camera entities. */
EPIX_EXPORT struct Camera2D {
    static void register_required_components(ecs::RequiredComponentsRegistrator& registrator);
};

/** @brief Bundle for spawning a complete 2D camera entity configured with
 * the core 2D render graph. */
EPIX_EXPORT struct Camera2DBundle {
    render::camera::Camera camera;
    render::camera::Projection projection;
    render::camera::CameraRenderGraph render_graph = Core2d;
    transform::Transform transform;
    render::view::VisibleEntities visible_entities;
    Camera2D camera_2d;
    /** @brief Which layers this camera renders. Default: all layers. */
    render::camera::RenderLayer render_layer = render::camera::RenderLayer::all();
};
}  // namespace epix::core_graph::core_2d

template <>
struct epix::ecs::Bundle<epix::core_graph::core_2d::Camera2DBundle> {
    static void get_components(core_graph::core_2d::Camera2DBundle& bundle,
                               std::invocable<utils::function_ref<void(void*)>> auto&& write_component) noexcept {
        write_component([&](void* ptr) { new (ptr) render::camera::Camera(std::move(bundle.camera)); });
        write_component([&](void* ptr) { new (ptr) render::camera::Projection(std::move(bundle.projection)); });
        write_component(
            [&](void* ptr) { new (ptr) render::camera::CameraRenderGraph(std::move(bundle.render_graph)); });
        write_component([&](void* ptr) { new (ptr) transform::Transform(std::move(bundle.transform)); });
        write_component(
            [&](void* ptr) { new (ptr) render::view::VisibleEntities(std::move(bundle.visible_entities)); });
        write_component([&](void* ptr) { new (ptr) core_graph::core_2d::Camera2D(std::move(bundle.camera_2d)); });
        write_component([&](void* ptr) { new (ptr) render::camera::RenderLayer(std::move(bundle.render_layer)); });
    }
    static std::array<std::optional<TypeId>, 7> type_ids(const ecs::Components& components) {
        return std::array{
            components.get_id<render::camera::Camera>(),
            components.get_id<render::camera::Projection>(),
            components.get_id<render::camera::CameraRenderGraph>(),
            components.get_id<transform::Transform>(),
            components.get_id<render::view::VisibleEntities>(),
            components.get_id<core_graph::core_2d::Camera2D>(),
            components.get_id<render::camera::RenderLayer>(),
        };
    }
    static std::vector<TypeId> register_components(ecs::ComponentsRegistrator& components) {
        std::vector<TypeId> ids;
        ids.push_back(components.template register_component<render::camera::Camera>());
        ids.push_back(components.template register_component<render::camera::Projection>());
        ids.push_back(components.template register_component<render::camera::CameraRenderGraph>());
        ids.push_back(components.template register_component<transform::Transform>());
        ids.push_back(components.template register_component<render::view::VisibleEntities>());
        ids.push_back(components.template register_component<core_graph::core_2d::Camera2D>());
        ids.push_back(components.template register_component<render::camera::RenderLayer>());
        return ids;
    }
};
static_assert(epix::ecs::is_bundle<epix::core_graph::core_2d::Camera2DBundle>);
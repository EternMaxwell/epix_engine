#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <epix/assets.hpp>
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
    /** @brief Final node that blits the main texture to the output. */
    BlitToOutput,
};

/**
 * @brief A transparent 2D render phase item.
 *
 * Sorted by inverse depth for back-to-front rendering. Supports instanced
 * batching.
 */
EPIX_EXPORT struct Transparent2D {
    /** @brief The render entity and its main-world entity (Bevy
     * representative_entity: (Entity, MainEntity)). */
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    /** @brief Depth value for sorting (inverted for back-to-front). */
    float depth;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Instance range covered by this item's batch (Bevy batch_range:
     * Range<u32>). */
    std::pair<std::uint32_t, std::uint32_t> batch_range;

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    float sort_key() const noexcept { return -depth; }  // inverse depth for back-to-front rendering
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return render::phase::PhaseItemExtraIndex::None; }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Transparent2D>);

/** @brief An opaque 2D render phase item.
 *
 * Sorted by OpaqueSortKey for front-to-back rendering and batching.
 */
EPIX_EXPORT struct Opaque2D {
    /** @brief The render entity and its main-world entity (Bevy
     * representative_entity: (Entity, MainEntity)). */
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Instance range covered by this item's batch (Bevy batch_range:
     * Range<u32>). */
    std::pair<std::uint32_t, std::uint32_t> batch_range;
    /** @brief Sort key for front-to-back opaque ordering. */
    render::phase::OpaqueSortKey batch_key;

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    const render::phase::OpaqueSortKey& sort_key() const noexcept { return batch_key; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return render::phase::PhaseItemExtraIndex::None; }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Opaque2D>);

/** @brief A UI 2D render phase item.
 *
 * Sorted by `order` for z-ordering of UI elements.
 */
EPIX_EXPORT struct UI2DItem {
    /** @brief The render entity and its main-world entity (Bevy
     * representative_entity: (Entity, MainEntity)). */
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    /** @brief Z-order for UI stacking (higher = on top). */
    int order;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Instance range covered by this item's batch (Bevy batch_range:
     * Range<u32>). */
    std::pair<std::uint32_t, std::uint32_t> batch_range;

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    int sort_key() const noexcept { return order; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return render::phase::PhaseItemExtraIndex::None; }
};

template <typename P>
struct Node2D : render::graph::Node {
    std::optional<ecs::QueryState<ecs::Item<const render::view::ExtractedView&,
                                            const render::camera::ExtractedCamera&,
                                            const render::view::ViewTarget&,
                                            const render::view::ViewDepthTexture&,
                                            const render::phase::RenderPhase<P>&>,
                                  ecs::Filter<>>>
        views;
    void update(ecs::World& world) override {
        if (!views) {
            views = world.try_query<ecs::Item<const render::view::ExtractedView&, const render::camera::ExtractedCamera&,
                                              const render::view::ViewTarget&, const render::view::ViewDepthTexture&,
                                              const render::phase::RenderPhase<P>&>>();
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
        auto&& [exview, camera, target, depth, phase] = *view_opt;
        auto render_pass                      = render_ctx.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor()
                .setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                    .setView(target.texture_view)
                                                    .setDepthSlice(~0u)
                                                    .setLoadOp(wgpu::LoadOp::eLoad)
                                                    .setStoreOp(wgpu::StoreOp::eStore)})
                .setDepthStencilAttachment(depth.attachment.get_attachment(wgpu::StoreOp::eStore)));
        // Bevy main_opaque_pass_2d_node set_camera_viewport: clip the main pass
        // to the camera viewport (origin, size, depth range).
        if (camera.viewport) {
            const auto& vp = *camera.viewport;
            render_pass.setViewport(static_cast<float>(vp.pos.x), static_cast<float>(vp.pos.y),
                                    static_cast<float>(vp.size.x), static_cast<float>(vp.size.y),
                                    vp.depth_range.first, vp.depth_range.second);
        }
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

/** @brief Embedded slang shaders for the final output blit (Bevy
 * core_pipeline upscaling: samples the main texture, writes to the output). */
EPIX_EXPORT struct Core2dBlitHandles {
    assets::Handle<shader::Shader> vertex_shader;
    assets::Handle<shader::Shader> fragment_shader;
};

/** @brief Per-format blit pipeline resources (Bevy BlitPipeline). */
EPIX_EXPORT struct Core2dBlitPipeline {
    wgpu::BindGroupLayout layout;
    wgpu::Sampler sampler;
    wgpu::Buffer vertex_buffer;  // 3 float2 UVs of a fullscreen triangle
    render::CachedPipelineId pipeline_id;
    wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
    /** @brief Whether this pipeline alpha-blends over the output (Bevy
     * ALPHA_BLENDING for cameras with sorted_camera_index_for_target > 0). */
    bool blend = false;
    bool ready = false;
};

/** @brief Final node of the 2D graph: copies the main texture to the view's
 * output attachment (the swapchain for window cameras) and marks it for
 * present (Bevy core_pipeline `upscaling`). */
EPIX_EXPORT struct Core2dBlitNode : render::graph::Node {
    std::optional<ecs::QueryState<ecs::Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>, ecs::Filter<>>> views;
    /** @brief Lazily-created per-format blit pipeline (node lives in the
     * render graph, so its members persist across frames). */
    std::optional<Core2dBlitPipeline> blit;
    void update(ecs::World& world) override;
    void run(render::graph::GraphContext& ctx, render::graph::RenderContext& render_ctx, const ecs::World& world) override;
};

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
    camera::Camera camera;
    camera::Projection projection;
    render::camera::CameraRenderGraph render_graph = Core2d;
    transform::Transform transform;
    camera::VisibleEntities visible_entities;
    Camera2D camera_2d;
    /** @brief Which layers this camera renders. Bevy default: layer 0 only
     * (a camera without an explicit RenderLayers sees layer 0). */
    camera::RenderLayers render_layer = camera::RenderLayers::layer(0);
};
}  // namespace epix::core_graph::core_2d

template <>
struct epix::ecs::Bundle<epix::core_graph::core_2d::Camera2DBundle> {
    static void get_components(core_graph::core_2d::Camera2DBundle& bundle,
                               std::invocable<utils::function_ref<void(void*)>> auto&& write_component) noexcept {
        write_component([&](void* ptr) { new (ptr) camera::Camera(std::move(bundle.camera)); });
        write_component([&](void* ptr) { new (ptr) camera::Projection(std::move(bundle.projection)); });
        write_component(
            [&](void* ptr) { new (ptr) render::camera::CameraRenderGraph(std::move(bundle.render_graph)); });
        write_component([&](void* ptr) { new (ptr) transform::Transform(std::move(bundle.transform)); });
        write_component(
            [&](void* ptr) { new (ptr) camera::VisibleEntities(std::move(bundle.visible_entities)); });
        write_component([&](void* ptr) { new (ptr) core_graph::core_2d::Camera2D(std::move(bundle.camera_2d)); });
        write_component([&](void* ptr) { new (ptr) camera::RenderLayers(std::move(bundle.render_layer)); });
    }
    static std::array<std::optional<TypeId>, 7> type_ids(const ecs::Components& components) {
        return std::array{
            components.get_id<camera::Camera>(),
            components.get_id<camera::Projection>(),
            components.get_id<render::camera::CameraRenderGraph>(),
            components.get_id<transform::Transform>(),
            components.get_id<camera::VisibleEntities>(),
            components.get_id<core_graph::core_2d::Camera2D>(),
            components.get_id<camera::RenderLayers>(),
        };
    }
    static std::vector<TypeId> register_components(ecs::ComponentsRegistrator& components) {
        std::vector<TypeId> ids;
        ids.push_back(components.template register_component<camera::Camera>());
        ids.push_back(components.template register_component<camera::Projection>());
        ids.push_back(components.template register_component<render::camera::CameraRenderGraph>());
        ids.push_back(components.template register_component<transform::Transform>());
        ids.push_back(components.template register_component<camera::VisibleEntities>());
        ids.push_back(components.template register_component<core_graph::core_2d::Camera2D>());
        ids.push_back(components.template register_component<camera::RenderLayers>());
        return ids;
    }
};
static_assert(epix::ecs::is_bundle<epix::core_graph::core_2d::Camera2DBundle>);
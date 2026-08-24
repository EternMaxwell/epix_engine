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
    /** @brief Dynamic-offset or indirect-parameter index assigned while batching. */
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    float sort_key() const noexcept { return -depth; }  // inverse depth for back-to-front rendering
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return extra_index_value; }
    void set_extra_index(render::phase::PhaseItemExtraIndex value) noexcept { extra_index_value = value; }
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
    /** @brief Dynamic-offset or indirect-parameter index assigned while batching. */
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    const render::phase::OpaqueSortKey& sort_key() const noexcept { return batch_key; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return extra_index_value; }
    void set_extra_index(render::phase::PhaseItemExtraIndex value) noexcept { extra_index_value = value; }
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
    /** @brief Dynamic-offset or indirect-parameter index assigned while batching. */
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    int sort_key() const noexcept { return order; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    render::phase::PhaseItemExtraIndex extra_index() const noexcept { return extra_index_value; }
    void set_extra_index(render::phase::PhaseItemExtraIndex value) noexcept { extra_index_value = value; }
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
            views =
                world.try_query<ecs::Item<const render::view::ExtractedView&, const render::camera::ExtractedCamera&,
                                          const render::view::ViewTarget&, const render::view::ViewDepthTexture&,
                                          const render::phase::RenderPhase<P>&>>();
        } else {
            views->update_archetypes(world);
        }
    }
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& ctx,
                                                          render::graph::RenderContext& render_ctx,
                                                          const ecs::World& world) override {
        // No matching render-world query yet: this is the same benign
        // no-view condition that Bevy's ViewNodeRunner treats as Ok(()).
        // Once the query exists, actual graph invocation failures are returned
        // through NodeRunError rather than being skipped.
        if (!views) return {};
        auto view_entity = ctx.view_entity();
        auto view_opt = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view_entity);
        if (!view_opt) return {};
        auto&& [exview, camera, target, depth, phase] = *view_opt;
        auto render_pass                              = render_ctx.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor()
                // ViewTarget owns the MSAA sample attachment and resolve
                // texture.  Using its attachment is required for Bevy's
                // default 4x MSAA; binding texture_view directly would pair
                // a single-sample color target with a multisampled depth
                // target.
                .setColorAttachments(std::array{target.get_color_attachment()})
                .setDepthStencilAttachment(depth.get_attachment(wgpu::StoreOp::eStore)));
        // Bevy main_opaque_pass_2d_node set_camera_viewport: clip the main pass
        // to the camera viewport (origin, size, depth range).
        if (camera.viewport) {
            const auto& vp = *camera.viewport;
            render_pass.setViewport(static_cast<float>(vp.physical_position.x), static_cast<float>(vp.physical_position.y),
                                    static_cast<float>(vp.physical_size.x), static_cast<float>(vp.physical_size.y), vp.depth.first,
                                    vp.depth.second);
        }
        phase.render(render_pass, world, view_entity);
        render_pass.end();
        render_ctx.flush_encoder();
        return {};
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
    /** @brief Exact output blend configuration used to specialize this
     * pipeline (including Bevy's automatic
     * later-camera alpha blend). */
    std::optional<wgpu::BlendState> output_blend;
};

/** @brief Render-world cache of Core2D output pipelines. Pipelines are
 * created/queued by the Queue stage, never by
 * the render graph. */
EPIX_EXPORT struct Core2dBlitPipelines {
    std::vector<Core2dBlitPipeline> pipelines;
};

/** @brief Final node of the 2D graph: copies the main texture to the view's
 * output attachment (the swapchain for window cameras) and marks it for
 * present (Bevy core_pipeline `upscaling`). */
EPIX_EXPORT struct Core2dBlitNode : render::graph::Node {
    std::optional<ecs::QueryState<ecs::Item<const render::camera::ExtractedCamera&, const render::view::ViewTarget&>,
                                  ecs::Filter<>>>
        views;
    void update(ecs::World& world) override;
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& ctx,
                                                          render::graph::RenderContext& render_ctx,
                                                          const ecs::World& world) override;
};

/** @brief Plugin that sets up the core 2D render graph and camera
 * projection. */
EPIX_EXPORT struct Core2dPlugin {
    void attach(app::App& app);
};

}  // namespace epix::core_graph::core_2d

#pragma once

#include <array>
#include <cstddef>
#include <epix/core.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <optional>
#include <span>
#include <utility>
#include <webgpu/webgpu.hpp>

namespace epix::core_graph::core_2d {

/** @brief Node labels for the 2D render graph passes. */
enum class Core2dNodes {
    StartMainPass,
    MainTransparentPass,
    MainOpaquePass,
    EndMainPass,
    ScreenUIPass,
};

struct Transparent2D {
    epix::core::Entity id;
    float depth;
    epix::render::CachedPipelineId pipeline_id;
    epix::render::phase::DrawFunctionId draw_func;
    std::size_t batch_count;

    epix::core::Entity entity() const noexcept { return id; }
    float sort_key() const noexcept { return -depth; }
    epix::render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    epix::render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    std::size_t batch_size() const noexcept { return batch_count; }
};
static_assert(epix::render::phase::BatchedPhaseItem<Transparent2D>);
static_assert(epix::render::phase::CachedRenderPipelinePhaseItem<Transparent2D>);

struct Opaque2D {
    epix::core::Entity id;
    epix::render::CachedPipelineId pipeline_id;
    epix::render::phase::DrawFunctionId draw_func;
    std::size_t batch_count;
    epix::render::phase::OpaqueSortKey batch_key;

    epix::core::Entity entity() const noexcept { return id; }
    const epix::render::phase::OpaqueSortKey& sort_key() const noexcept { return batch_key; }
    epix::render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    epix::render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    std::size_t batch_size() const noexcept { return batch_count; }
};
static_assert(epix::render::phase::BatchedPhaseItem<Opaque2D>);
static_assert(epix::render::phase::CachedRenderPipelinePhaseItem<Opaque2D>);

struct UI2DItem {
    epix::core::Entity id;
    int order;
    epix::render::CachedPipelineId pipeline_id;
    epix::render::phase::DrawFunctionId draw_func;
    std::size_t batch_count;

    epix::core::Entity entity() const noexcept { return id; }
    int sort_key() const noexcept { return order; }
    epix::render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    epix::render::CachedPipelineId pipeline() const noexcept { return pipeline_id; }
    std::size_t batch_size() const noexcept { return batch_count; }
};

template <typename P>
struct Node2D : epix::render::graph::Node {
    std::optional<epix::core::QueryState<epix::core::Item<const epix::render::view::ExtractedView&,
                                                          const epix::render::view::ViewTarget&,
                                                          const epix::render::view::ViewDepth&,
                                                          const epix::render::phase::RenderPhase<P>&>,
                                         epix::core::Filter<>>>
        views;
    void update(const epix::core::World& world) override {
        if (!views) {
            views = world.template try_query<
                epix::core::Item<const epix::render::view::ExtractedView&, const epix::render::view::ViewTarget&,
                                 const epix::render::view::ViewDepth&, const epix::render::phase::RenderPhase<P>&>>();
        } else {
            views->update_archetypes(world);
        }
    }
    void run(epix::render::graph::GraphContext& ctx,
             epix::render::graph::RenderContext& render_ctx,
             const epix::core::World& world) override {
        if (!views) return;
        auto view_entity = ctx.view_entity();
        auto view_opt = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view_entity);
        if (!view_opt) return;
        auto&& [exview, target, depth, phase] = *view_opt;
        auto render_pass                      = render_ctx.command_encoder().beginRenderPass(
            wgpu::RenderPassDescriptor()
                .setColorAttachments(std::array{wgpu::RenderPassColorAttachment{}
                                                    .setView(target.texture_view)
                                                    .setDepthSlice(~0u)
                                                    .setLoadOp(wgpu::LoadOp::eLoad)
                                                    .setStoreOp(wgpu::StoreOp::eStore)})
                .setDepthStencilAttachment(wgpu::RenderPassDepthStencilAttachment{}
                                               .setView(depth.depth_view)
                                               .setDepthLoadOp(wgpu::LoadOp::eLoad)
                                               .setDepthStoreOp(wgpu::StoreOp::eStore)));
        phase.render(render_pass, world, view_entity);
        render_pass.end();
        render_ctx.flush_encoder();
    }
};

inline struct Core2dGraph {
    void add_to(epix::render::graph::RenderGraph& g);
} Core2d;

struct Core2dPlugin {
    void attach(epix::core::App& app);
};

struct Camera2D {
    static void register_required_components(epix::core::Components& components);
};

struct Camera2DBundle {
    epix::render::camera::Camera camera;
    epix::render::camera::Projection projection;
    epix::render::camera::CameraRenderGraph render_graph = Core2d;
    epix::transform::Transform transform;
    epix::render::view::VisibleEntities visible_entities;
    Camera2D camera_2d;
    epix::render::camera::RenderLayer render_layer = epix::render::camera::RenderLayer::all();
};

}  // namespace epix::core_graph::core_2d

template <>
struct epix::core::Bundle<epix::core_graph::core_2d::Camera2DBundle> {
    static void get_components(
        epix::core_graph::core_2d::Camera2DBundle& bundle,
        epix::utils::function_ref<void(epix::utils::function_ref<void(void*)>)> write_component) noexcept {
        write_component([&](void* ptr) { new (ptr) epix::render::camera::Camera(std::move(bundle.camera)); });
        write_component([&](void* ptr) { new (ptr) epix::render::camera::Projection(std::move(bundle.projection)); });
        write_component(
            [&](void* ptr) { new (ptr) epix::render::camera::CameraRenderGraph(std::move(bundle.render_graph)); });
        write_component([&](void* ptr) { new (ptr) epix::transform::Transform(std::move(bundle.transform)); });
        write_component(
            [&](void* ptr) { new (ptr) epix::render::view::VisibleEntities(std::move(bundle.visible_entities)); });
        write_component([&](void* ptr) { new (ptr) epix::core_graph::core_2d::Camera2D(std::move(bundle.camera_2d)); });
        write_component(
            [&](void* ptr) { new (ptr) epix::render::camera::RenderLayer(std::move(bundle.render_layer)); });
    }
    static std::array<epix::core::TypeId, 7> type_ids(const epix::core::TypeRegistry& registry) {
        return std::array{
            registry.template type_id<epix::render::camera::Camera>(),
            registry.template type_id<epix::render::camera::Projection>(),
            registry.template type_id<epix::render::camera::CameraRenderGraph>(),
            registry.template type_id<epix::transform::Transform>(),
            registry.template type_id<epix::render::view::VisibleEntities>(),
            registry.template type_id<epix::core_graph::core_2d::Camera2D>(),
            registry.template type_id<epix::render::camera::RenderLayer>(),
        };
    }
    static void register_components(const epix::core::TypeRegistry& registry, epix::core::Components& components) {
        components.template register_info<epix::render::camera::Camera>();
        components.template register_info<epix::render::camera::Projection>();
        components.template register_info<epix::render::camera::CameraRenderGraph>();
        components.template register_info<epix::transform::Transform>();
        components.template register_info<epix::render::view::VisibleEntities>();
        components.template register_info<epix::core_graph::core_2d::Camera2D>();
        components.template register_info<epix::render::camera::RenderLayer>();
    }
};
static_assert(epix::core::is_bundle<epix::core_graph::core_2d::Camera2DBundle>);

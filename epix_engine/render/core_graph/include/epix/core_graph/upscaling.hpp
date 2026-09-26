#pragma once


#ifndef EPIX_CXX_MODULE
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/core_graph/blit.hpp>

namespace epix::core_graph {

/** @brief Per-view selected output pipeline (Bevy `ViewUpscalingPipeline`). */
EPIX_EXPORT struct ViewUpscalingPipeline {
    render::CachedPipelineId pipeline_id;
};

/** @brief Typed output-copy render-graph node (Bevy `UpscalingNode`). */
EPIX_EXPORT struct EPIX_API UpscalingNode {
    using ViewQuery = ecs::Item<const render::view::ViewTarget&,
                                const ViewUpscalingPipeline&,
                                ecs::Opt<const render::camera::ExtractedCamera&>>;

    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& graph,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;

   private:
    // Bevy caches this by TextureViewId. Direct wgpu resources are the
    // accepted Epix representation of that identifier.
    struct BindGroupCache {
        std::mutex mutex;
        std::optional<std::pair<wgpu::TextureView, wgpu::BindGroup>> value;
    };
    std::shared_ptr<BindGroupCache> cached_bind_group = std::make_shared<BindGroupCache>();
};

/** @brief Installs per-view output-pipeline preparation and the Bevy-style
 * `Upscaling` graph node. */
EPIX_EXPORT struct EPIX_API UpscalingPlugin {
    void attach(app::App& app);
};

}  // namespace epix::core_graph

#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/render/extract.hpp>
#include <optional>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::experimental {

/** @brief Enables experimental GPU occlusion culling for a view (Bevy
 * `experimental::occlusion_culling::OcclusionCulling`). It is extracted to
 * the render world; the batching/preprocessing path consumes it when enabled. */
EPIX_EXPORT struct OcclusionCulling {};

/** @brief Render-world depth data for an occlusion-culling subview (Bevy
 * `OcclusionCullingSubview`). */
EPIX_EXPORT struct OcclusionCullingSubview {
    wgpu::TextureView depth_texture_view;
    std::uint32_t depth_texture_size = 0;
};

/** @brief Render-world entities associated with an occlusion-culling view
 * (Bevy `OcclusionCullingSubviewEntities`). */
EPIX_EXPORT struct OcclusionCullingSubviewEntities {
    std::vector<epix::ecs::Entity> entities;
};

}  // namespace epix::render::experimental

namespace epix::render {
template <>
struct ExtractComponent<experimental::OcclusionCulling> {
    using QueryData   = const experimental::OcclusionCulling&;
    using QueryFilter = epix::ecs::Filter<>;
    using Out         = experimental::OcclusionCulling;
    static std::optional<Out> extract_component(QueryData) { return Out{}; }
};
}  // namespace epix::render

namespace epix::render::experimental {
/** @brief Registers extraction for `OcclusionCulling` (Bevy
 * `OcclusionCullingPlugin`). Shader/library loading is handled by Epix's
 * Slang preprocessing pipeline. */
EPIX_EXPORT struct OcclusionCullingPlugin {
    void attach(epix::app::App& app) const {
        app.add_plugins(epix::render::ExtractComponentPlugin<OcclusionCulling>{});
    }
};
}  // namespace epix::render::experimental

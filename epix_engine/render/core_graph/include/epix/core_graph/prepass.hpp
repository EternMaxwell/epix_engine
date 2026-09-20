#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/core_graph/core3d.hpp>

namespace epix::core_graph {

/** @brief The previous frame's view matrices, which the motion-vector prepass
 *  subtracts from the current ones (Bevy `PreviousViewData`,
 *  `bevy_core_pipeline/src/prepass/mod.rs:96-102`). The field order is Bevy's,
 *  because the shader reads them by offset. */
EPIX_EXPORT struct PreviousViewData {
    glm::mat4 view_from_world{1.0f};
    glm::mat4 clip_from_world{1.0f};
    glm::mat4 clip_from_view{1.0f};
    glm::mat4 world_from_clip{1.0f};
    glm::mat4 view_from_clip{1.0f};
};

}  // namespace epix::core_graph

namespace epix::render::render_resource {
/** @brief Five matrices laid out to match the shader's constant buffer. */
template <>
struct ShaderTypeInfo<::epix::core_graph::PreviousViewData>
    : RawShaderType<::epix::core_graph::PreviousViewData> {};
}  // namespace epix::render::render_resource

namespace epix::core_graph {

/** @brief Holds one `PreviousViewData` per view (Bevy `PreviousViewUniforms`,
 *  `prepass/mod.rs:105-108`). */
EPIX_EXPORT struct PreviousViewUniforms {
    render::render_resource::DynamicUniformBuffer<PreviousViewData> uniforms;
};

/** @brief The byte offset of a view's entry in `PreviousViewUniforms` (Bevy
 *  `PreviousViewUniformOffset`, `prepass/mod.rs:110-113`). */
EPIX_EXPORT struct PreviousViewUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief The prepass colour targets for a prepass set (Bevy
 *  `prepass_target_descriptors`, `prepass/mod.rs:383-405`): normals, motion
 *  vectors, the deferred G-buffer and the deferred lighting id, each present
 *  only when its prepass is. */
EPIX_EXPORT std::array<std::optional<wgpu::ColorTargetState>, 4> prepass_target_descriptors(
    bool normal_prepass, bool motion_vector_prepass, bool deferred_prepass);

/** @brief Uploads every view's `PreviousViewData` into `PreviousViewUniforms`
 *  and stamps the view with its offset (Bevy `prepare_previous_view_uniforms`,
 *  `bevy_pbr/src/prepass/mod.rs`).
 *
 *  Bevy fills the component in the main world (`update_previous_view_data`,
 *  `:189-206`) with the *current* camera matrices and extracts it one frame
 *  late, so the render world always reads the previous frame's; this system is
 *  the render-world half that turns those components into a buffer the prepass
 *  bind groups can point at. */
EPIX_EXPORT void prepare_previous_view_uniforms(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue,
    ecs::ResMut<PreviousViewUniforms> previous_view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const PreviousViewData&>> views);

}  // namespace epix::core_graph

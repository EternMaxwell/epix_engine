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
EPIX_EXPORT EPIX_API std::array<std::optional<wgpu::ColorTargetState>, 4> prepass_target_descriptors(
    bool normal_prepass, bool motion_vector_prepass, bool deferred_prepass);

/** @brief Converts a `prepass_target_descriptors` set into the target list a
 *  fragment stage can carry.
 *
 *  Bevy stores `Option<ColorTargetState>` per location and leaves unwritten ones
 *  `None`, but Epix's `FragmentState::targets` cannot hold a hole - the wrapper's
 *  targets are values (`webgpu.hpp:5882-5890`) and so is the C ABI's array
 *  (`webgpu.h:4794-4801`) - so an unwritten location is spelled as a target with
 *  an `Undefined` format. wgpu accepts that while the shader writes a later
 *  location, which is what the prepass skybox does (verified by
 *  `SkyboxPrepass.UndefinedFormatTargetIsAcceptedAsAnUnwrittenSlot` in
 *  `core_graph/tests/header/skybox_tests.cpp`). */
EPIX_EXPORT EPIX_API std::vector<wgpu::ColorTargetState> prepass_pipeline_targets(bool normal_prepass,
                                                                        bool motion_vector_prepass,
                                                                        bool deferred_prepass);

/** @brief Stores each camera's *current* matrices as `PreviousViewData`, which
 *  the render world reads one frame late and therefore as the previous frame's
 *  (Bevy `update_previous_view_data`, `bevy_pbr/src/prepass/mod.rs:189-206`).
 *
 *  Bevy schedules it in the PBR prepass; Epix's prepass nodes live in
 *  `core_graph`, so it belongs here, and it only needs the camera's global matrix
 *  and its projection. */
EPIX_EXPORT EPIX_API void update_previous_view_data(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity, const camera::Camera&, const transform::GlobalTransform&>,
               ecs::With<camera::Camera3d>>
        cameras);

/** @brief Uploads every view's `PreviousViewData` into `PreviousViewUniforms`
 *  and stamps the view with its offset (Bevy `prepare_previous_view_uniforms`,
 *  `bevy_pbr/src/prepass/mod.rs`).
 *
 *  Bevy fills the component in the main world (`update_previous_view_data`,
 *  `:189-206`) with the *current* camera matrices and extracts it one frame
 *  late, so the render world always reads the previous frame's; this system is
 *  the render-world half that turns those components into a buffer the prepass
 *  bind groups can point at. */
EPIX_EXPORT EPIX_API void prepare_previous_view_uniforms(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue,
    ecs::ResMut<PreviousViewUniforms> previous_view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const PreviousViewData&>> views);

}  // namespace epix::core_graph

namespace epix::render {
/** @brief Extracts the previous-frame view data onto the view, so the render
 *  world reads what the main world stored last frame (Bevy copies the component
 *  along with the camera during extraction). */
template <>
struct ExtractComponent<core_graph::PreviousViewData> {
    using QueryData   = const core_graph::PreviousViewData&;
    using QueryFilter = epix::ecs::Filter<>;
    using Out         = core_graph::PreviousViewData;
    static std::optional<Out> extract_component(const core_graph::PreviousViewData& item) { return item; }
};
}  // namespace epix::render

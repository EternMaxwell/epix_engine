#include <epix/core_graph/prepass.hpp>

namespace epix::core_graph {

std::array<std::optional<wgpu::ColorTargetState>, 4> prepass_target_descriptors(bool normal_prepass,
                                                                               bool motion_vector_prepass,
                                                                               bool deferred_prepass) {
    // Bevy `prepass_target_descriptors` (`prepass/mod.rs:383-405`): the four
    // slots are the normal target, the motion-vector target, the deferred
    // G-buffer and the deferred lighting id, and a slot is `nullopt` when that
    // prepass is not on. Bevy's `then_some` builds a target only when the flag is
    // set, with a `None` blend and the full write mask.
    const auto target = [](wgpu::TextureFormat format) {
        return wgpu::ColorTargetState().setFormat(format).setWriteMask(wgpu::ColorWriteMask::eAll);
    };

    return std::array<std::optional<wgpu::ColorTargetState>, 4>{
        normal_prepass ? std::optional{target(core_3d::NORMAL_PREPASS_FORMAT)} : std::nullopt,
        motion_vector_prepass ? std::optional{target(core_3d::MOTION_VECTOR_PREPASS_FORMAT)} : std::nullopt,
        deferred_prepass ? std::optional{target(core_3d::DEFERRED_PREPASS_FORMAT)} : std::nullopt,
        deferred_prepass ? std::optional{target(core_3d::DEFERRED_LIGHTING_PASS_ID_FORMAT)} : std::nullopt,
    };
}

std::vector<wgpu::ColorTargetState> prepass_pipeline_targets(bool normal_prepass, bool motion_vector_prepass,
                                                             bool deferred_prepass) {
    // Epix's fragment target list holds values, so a location the shader does not
    // write becomes a target with an `Undefined` format rather than a hole.
    const auto unwritten =
        wgpu::ColorTargetState().setFormat(wgpu::TextureFormat::eUndefined).setWriteMask(wgpu::ColorWriteMask::eAll);

    std::vector<wgpu::ColorTargetState> targets;
    for (const auto& descriptor : prepass_target_descriptors(normal_prepass, motion_vector_prepass, deferred_prepass)) {
        targets.push_back(descriptor.value_or(unwritten));
    }
    return targets;
}

void update_previous_view_data(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity, const camera::Camera&, const transform::GlobalTransform&>,
               ecs::With<camera::Camera3d>>
        cameras) {
    // Bevy `update_previous_view_data` (`bevy_pbr/src/prepass/mod.rs:189-206`):
    // the camera's global transform is the world-from-view matrix, and every other
    // matrix is derived from it and the projection.
    for (auto&& [entity, camera, camera_transform] : cameras.iter()) {
        const glm::mat4 world_from_view = camera_transform.matrix;
        const glm::mat4 view_from_world = glm::inverse(world_from_view);
        const glm::mat4 clip_from_view  = camera.clip_from_view();
        const glm::mat4 view_from_clip  = glm::inverse(clip_from_view);

        commands.entity(entity).insert(PreviousViewData{
            .view_from_world = view_from_world,
            .clip_from_world = clip_from_view * view_from_world,
            .clip_from_view  = clip_from_view,
            .world_from_clip = world_from_view * view_from_clip,
            .view_from_clip  = view_from_clip,
        });
    }
}

void prepare_previous_view_uniforms(ecs::Commands commands,
                                    ecs::Res<wgpu::Device> device,
                                    ecs::Res<wgpu::Queue> queue,
                                    ecs::ResMut<PreviousViewUniforms> previous_view_uniforms,
                                    ecs::Query<ecs::Item<ecs::Entity, const PreviousViewData&>> views) {
    // Bevy `prepare_previous_view_uniforms`: one entry per view, the byte offset
    // recorded on the view so a prepass bind group can pass it as the dynamic
    // offset, and a single upload for the frame (Bevy writes through
    // `DynamicUniformBuffer::write_buffer`).
    auto& uniforms = previous_view_uniforms->uniforms;
    uniforms.clear();
    for (auto&& [entity, previous_view_data] : views.iter()) {
        const std::size_t offset = uniforms.push(previous_view_data);
        commands.entity(entity).insert(PreviousViewUniformOffset{static_cast<std::uint32_t>(offset)});
    }
    uniforms.write_buffer(device.get(), queue.get());
}

}  // namespace epix::core_graph

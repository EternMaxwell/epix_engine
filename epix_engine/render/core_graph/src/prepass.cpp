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

std::vector<wgpu::ColorTargetState> prepass_pipeline_targets(bool normal_prepass,
                                                             bool motion_vector_prepass,
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

}  // namespace epix::core_graph

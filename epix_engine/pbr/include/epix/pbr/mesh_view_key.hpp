#pragma once

#include <cstdint>

#include <epix/common.hpp>
#include <epix/pbr/mesh_pipeline_key.hpp>
#include <epix/pbr/shadow_filtering_method.hpp>
#include <epix/render.hpp>
#include <epix/pbr/atmosphere.hpp>
#include <epix/pbr/fog.hpp>
#include <epix/core_graph/core3d.hpp>
#include <epix/core_graph/oit.hpp>
#include <epix/core_graph/tonemapping.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>

namespace epix::pbr {

/** @brief The per-view inputs the mesh key is assembled from (Bevy
 *  `check_views_need_specialization`, `bevy_pbr/src/render/mesh.rs:309-446`).
 *
 *  Bevy reads these from a fifteen-element query inside the system; a port-side struct
 *  is how this port states the same thing, the way its light-sizing helpers and
 *  `specialize_mesh_capability_shader_defs` state theirs. Nothing here is a new fact,
 *  only a named one. */
EPIX_EXPORT struct MeshViewKeyInputs {
    std::uint32_t msaa_samples = 1;
    bool hdr                     = false;
    bool normal_prepass          = false;
    bool depth_prepass           = false;
    bool motion_vector_prepass   = false;
    bool deferred_prepass        = false;
    bool temporal_jitter         = false;
    bool environment_map         = false;
    bool irradiance_volume       = false;
    bool oit_enabled             = false;
    bool atmosphere              = false;
    bool invert_culling          = false;
    /** @brief Bevy matches on `Projection`: perspective, orthographic, or nonstandard
     *  for the custom arm. */
    enum class ProjectionKind : std::uint8_t { Perspective, Orthographic, Nonstandard };
    ProjectionKind projection = ProjectionKind::Perspective;
    ShadowFilteringMethod shadow_filtering_method = SHADOW_FILTERING_METHOD_DEFAULT;
    bool tonemap_in_shader = false;
    bool deband_dither     = false;
    /** @brief `Has<ScreenSpaceAmbientOcclusion>`. **The component does not exist in
     *  this port** (`PBR-887`), so this bit is always clear here while Bevy sets it
     *  when SSAO is present - the one input `PBR-115` still lacks. */
    bool screen_space_ambient_occlusion = false;
    bool distance_fog = false;
};

/** @brief Assemble a view's mesh key (Bevy's `view_key` assembly, `mesh.rs:355-437`),
 *  in Bevy's order.
 *
 *  The tail of that assembly - `screen_space_specular_transmission_pipeline_key` from
 *  the camera's quality setting - is **not** included yet: its mapping is a separate
 *  Bevy helper this round did not read, and inventing it would be worse than leaving
 *  it out with a name. */
[[nodiscard]] EPIX_EXPORT inline MeshPipelineKey make_mesh_view_key(const MeshViewKeyInputs& inputs) noexcept {
    MeshPipelineKey key = MeshPipelineKey::from_msaa_samples(inputs.msaa_samples) |
                          MeshPipelineKey::from_hdr(inputs.hdr);

    if (inputs.normal_prepass) key |= MeshPipelineKey::NORMAL_PREPASS;
    if (inputs.depth_prepass) key |= MeshPipelineKey::DEPTH_PREPASS;
    if (inputs.motion_vector_prepass) key |= MeshPipelineKey::MOTION_VECTOR_PREPASS;
    if (inputs.deferred_prepass) key |= MeshPipelineKey::DEFERRED_PREPASS;
    if (inputs.temporal_jitter) key |= MeshPipelineKey::TEMPORAL_JITTER;
    if (inputs.environment_map) key |= MeshPipelineKey::ENVIRONMENT_MAP;
    if (inputs.irradiance_volume) key |= MeshPipelineKey::IRRADIANCE_VOLUME;
    if (inputs.oit_enabled) key |= MeshPipelineKey::OIT_ENABLED;
    if (inputs.atmosphere) key |= MeshPipelineKey::ATMOSPHERE;
    if (inputs.invert_culling) key |= MeshPipelineKey::INVERT_CULLING;

    switch (inputs.projection) {
        case MeshViewKeyInputs::ProjectionKind::Perspective:
            key |= MeshPipelineKey::VIEW_PROJECTION_PERSPECTIVE;
            break;
        case MeshViewKeyInputs::ProjectionKind::Orthographic:
            key |= MeshPipelineKey::VIEW_PROJECTION_ORTHOGRAPHIC;
            break;
        case MeshViewKeyInputs::ProjectionKind::Nonstandard:
            key |= MeshPipelineKey::VIEW_PROJECTION_NONSTANDARD;
            break;
    }

    // Spelled through the mapping rather than by enumerating the arms again, which is
    // what the mapping exists for: `Hardware2x2` is this field's zero value.
    key |= shadow_filter_method_bits(inputs.shadow_filtering_method);

    if (inputs.tonemap_in_shader) key |= MeshPipelineKey::TONEMAP_IN_SHADER;
    if (inputs.deband_dither) key |= MeshPipelineKey::DEBAND_DITHER;
    if (inputs.screen_space_ambient_occlusion) key |= MeshPipelineKey::SCREEN_SPACE_AMBIENT_OCCLUSION;
    if (inputs.distance_fog) key |= MeshPipelineKey::DISTANCE_FOG;
    return key;
}

}  // namespace epix::pbr

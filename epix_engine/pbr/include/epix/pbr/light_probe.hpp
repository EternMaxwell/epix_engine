#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <epix/common.hpp>
#endif
#include <epix/pbr/probe.hpp>

namespace epix::pbr {

/** @brief The most probes of each kind a view considers (Bevy `MAX_VIEW_LIGHT_PROBES`,
 *  `light_probe/mod.rs:47-51`).
 *
 *  Small on purpose: the fragment shader linearly searches these lists per fragment. */
EPIX_EXPORT inline constexpr std::size_t MAX_VIEW_LIGHT_PROBES = 8;

/** @brief Texture bindings the fragment shader uses, not counting environment maps or
 *  irradiance volumes (Bevy `STANDARD_MATERIAL_FRAGMENT_SHADER_MIN_TEXTURE_BINDINGS`,
 *  `light_probe/mod.rs:53-55`). Used to work out how many probes fit in the binding
 *  arrays. */
EPIX_EXPORT inline constexpr std::size_t STANDARD_MATERIAL_FRAGMENT_SHADER_MIN_TEXTURE_BINDINGS = 16;

/** @brief One light probe as the shader reads it (Bevy `RenderLightProbe`,
 *  `light_probe/mod.rs:64-86`).
 *
 *  The transform is stored **transposed**, as three rows, because that is the form the
 *  bounding-box test in the shader wants: it transforms the fragment into the probe's
 *  space rather than the other way round.
 *
 *  The layout is 64 bytes, not the 60 the fields add up to, because a struct holding a
 *  `vec4` array aligns to 16 in std140 - and 64 is the stride an array of these needs
 *  inside the per-view uniform. The padding is explicit for the same reason as
 *  `GpuFog`'s: glm's `vec4` is not 16-byte aligned by default, so the struct would
 *  otherwise come out at 60 and every probe after the first would be misread. */
EPIX_EXPORT struct RenderLightProbe {
    /** @brief The world-to-probe transform, transposed into three rows. */
    std::array<glm::vec4, 3> light_from_world_transposed{};
    /** @brief The probe's index in the texture array or arrays. */
    std::int32_t texture_index = 0;
    /** @brief Scale applied to the light this probe contributes. */
    float intensity = 0.0f;
    /** @brief Whether this probe adds to the diffuse contribution for lightmapped
     *  meshes: 1 if it does, 0 otherwise. */
    std::uint32_t affects_lightmapped_mesh_diffuse = 0;
    /** @brief Explicit std140 tail padding; see the class comment. */
    std::uint32_t tail_padding = 0;
};
static_assert(sizeof(RenderLightProbe) == 64, "RenderLightProbe must match the shader's layout");
static_assert(offsetof(RenderLightProbe, light_from_world_transposed) == 0);
static_assert(offsetof(RenderLightProbe, texture_index) == 48);
static_assert(offsetof(RenderLightProbe, intensity) == 52);
static_assert(offsetof(RenderLightProbe, affects_lightmapped_mesh_diffuse) == 56);
static_assert(offsetof(RenderLightProbe, tail_padding) == 60);

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief `RenderLightProbe` is uploaded as raw bytes; the struct is already padded to
 *  the shader's layout with every offset asserted, so a derived layout has nothing to
 *  add. */
template <>
struct ShaderTypeInfo<::epix::pbr::RenderLightProbe> : RawShaderType<::epix::pbr::RenderLightProbe> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The light probes a view takes into account, as the shader reads them (Bevy
 *  `LightProbesUniform`, `light_probe/mod.rs:88-125`).
 *
 *  Both lists are sorted nearest to farthest from the camera and padded out to
 *  `MAX_VIEW_LIGHT_PROBES` entries, with the counts saying how many are real: the
 *  shader walks the list in order and stops at the count, so the entries past it are
 *  never read but still occupy the layout.
 *
 *  The uniform's size follows from std140 rather than from the fields adding up. Each
 *  array element is a `RenderLightProbe` at its 64-byte stride, so the two arrays are
 *  512 bytes each; the six scalars then pack into 24 contiguous bytes, and the struct
 *  rounds up to a multiple of 16. The tail padding makes that explicit - without it the
 *  struct would come out at 1048 and the view environment map fields would sit in the
 *  wrong place for every array after the first. */
EPIX_EXPORT struct LightProbesUniform {
    std::array<RenderLightProbe, MAX_VIEW_LIGHT_PROBES> reflection_probes{};
    std::array<RenderLightProbe, MAX_VIEW_LIGHT_PROBES> irradiance_volumes{};
    /** @brief How many reflection probes in the list are real. */
    std::int32_t reflection_probe_count = 0;
    /** @brief How many irradiance volumes in the list are real. */
    std::int32_t irradiance_volume_count = 0;
    /** @brief The view's own environment maps, used when no probe contains the
     *  fragment. */
    std::int32_t view_cubemap_index = 0;
    /** @brief The smallest valid specular mip level for the view's cubemap. */
    std::uint32_t smallest_specular_mip_level_for_view = 0;
    /** @brief The intensity of the view's environment cubemap. */
    float intensity_for_view = 0.0f;
    /** @brief Whether the view's environment map affects lightmapped meshes: 1 if it
     *  does, 0 otherwise. */
    std::uint32_t view_environment_map_affects_lightmapped_mesh_diffuse = 0;
    /** @brief Explicit std140 tail padding; see the class comment. */
    std::array<std::uint32_t, 2> tail_padding{};
};
static_assert(sizeof(LightProbesUniform) == 1056, "LightProbesUniform must match the shader's layout");
static_assert(offsetof(LightProbesUniform, reflection_probes) == 0);
static_assert(offsetof(LightProbesUniform, irradiance_volumes) == 512);
static_assert(offsetof(LightProbesUniform, reflection_probe_count) == 1024);
static_assert(offsetof(LightProbesUniform, view_cubemap_index) == 1032);
static_assert(offsetof(LightProbesUniform, tail_padding) == 1048);

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief `LightProbesUniform` is uploaded as raw bytes; the struct is already padded
 *  to the shader's std140 layout with its offsets asserted. */
template <>
struct ShaderTypeInfo<::epix::pbr::LightProbesUniform>
    : RawShaderType<::epix::pbr::LightProbesUniform> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief One `LightProbesUniform` per view (Bevy `LightProbesBuffer`,
 *  `light_probe/mod.rs:127-129`). */
EPIX_EXPORT struct LightProbesBuffer {
    render::render_resource::DynamicUniformBuffer<LightProbesUniform> uniforms;
};

/** @brief A view's offset into the light probe buffer (Bevy
 *  `ViewLightProbesUniformOffset`, `light_probe/mod.rs:131-134`). */
EPIX_EXPORT struct ViewLightProbesUniformOffset {
    std::uint32_t offset = 0;
};

/** @brief The uniform built from an `EnvironmentMapLight` and read by the environment
 *  map shader (Bevy `EnvironmentMapUniform`, `light_probe/mod.rs:266-280`).
 *
 *  One matrix: the world-space transform of the sample ray for the cubemap. The
 *  identity default is meaningful rather than a placeholder - a probe with no rotation
 *  samples the cubemap unrotated. */
EPIX_EXPORT struct EnvironmentMapUniform {
    glm::mat4 transform{1.0f};
};

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief The uniform is one `mat4` uploaded as raw bytes, so a derived layout has
 *  nothing to add. This must precede `EnvironmentMapUniformBuffer`, which instantiates
 *  `DynamicUniformBuffer<EnvironmentMapUniform>` and checks the `ShaderWritable`
 *  concept there - the same ordering fog's uniform needs. */
template <>
struct ShaderTypeInfo<::epix::pbr::EnvironmentMapUniform>
    : RawShaderType<::epix::pbr::EnvironmentMapUniform> {};
}  // namespace epix::render::render_resource

namespace epix::pbr {

/** @brief The environment map settings per view (Bevy `EnvironmentMapUniformBuffer`,
 *  `light_probe/mod.rs:282-284`). */
EPIX_EXPORT struct EnvironmentMapUniformBuffer {
    render::render_resource::DynamicUniformBuffer<EnvironmentMapUniform> uniforms;
};

/** @brief A view's offset into the environment map buffer (Bevy
 *  `ViewEnvironmentMapUniformOffset`, `light_probe/mod.rs:286-289`). */
EPIX_EXPORT struct ViewEnvironmentMapUniformOffset {
    std::uint32_t offset = 0;
};

}  // namespace epix::pbr

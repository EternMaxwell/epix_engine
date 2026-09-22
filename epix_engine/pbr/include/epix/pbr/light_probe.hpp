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

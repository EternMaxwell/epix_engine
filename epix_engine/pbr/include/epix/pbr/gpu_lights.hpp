#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#endif

namespace epix::pbr {

/** @brief The most directional lights Bevy supports (`light.rs:185`); WebGL2
 *  without WebGPU caps it at one, which Epix does not target. */
EPIX_EXPORT inline constexpr std::size_t MAX_DIRECTIONAL_LIGHTS = 10;
/** @brief The most cascades one directional light may have
 *  (`light.rs:191`). */
EPIX_EXPORT inline constexpr std::size_t MAX_CASCADES_PER_LIGHT = 4;

/** @brief One directional light's cascade as the shader sees it (Bevy
 *  `GpuDirectionalCascade`, `render/light.rs:124-129`).
 *
 *  The eight trailing padding bytes are not decoration: a shader type's size
 *  rounds up to its largest member's alignment, so Bevy's layout is 80 bytes
 *  (a 64-byte matrix, two floats, then padding to 16), where C++ would lay the
 *  struct out as 72. The same rule that caught the skybox uniform applies. */
EPIX_EXPORT struct GpuDirectionalCascade {
    glm::mat4 clip_from_world{1.0f};
    float texel_size = 0.0f;
    float far_bound  = 0.0f;
    /** @brief Padding to the shader type's 16-byte-aligned size. */
    std::array<float, 2> padding{0.0f, 0.0f};
};
static_assert(sizeof(GpuDirectionalCascade) == 80, "the shader type is 80 bytes");

/** @brief The bits a directional light's `flags` word carries; these must match
 *  the flags in `mesh_view_types.wgsl` (Bevy `DirectionalLightFlags`,
 *  `light.rs:148-158`). */
EPIX_EXPORT struct DirectionalLightFlags {
    static constexpr std::uint32_t SHADOWS_ENABLED                  = 1u << 0;
    static constexpr std::uint32_t VOLUMETRIC                       = 1u << 1;
    static constexpr std::uint32_t AFFECTS_LIGHTMAPPED_MESH_DIFFUSE = 1u << 2;
    static constexpr std::uint32_t NONE                             = 0;
    static constexpr std::uint32_t UNINITIALIZED                    = 0xFFFF;
};

/** @brief One directional light as the shader sees it (Bevy
 *  `GpuDirectionalLight`, `light.rs:131-146`). Its size is already a multiple of
 *  sixteen, so no tail padding is needed. */
EPIX_EXPORT struct GpuDirectionalLight {
    std::array<GpuDirectionalCascade, MAX_CASCADES_PER_LIGHT> cascades{};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 dir_to_light{0.0f, 0.0f, -1.0f};
    std::uint32_t flags                    = DirectionalLightFlags::NONE;
    float soft_shadow_size                 = 0.0f;
    float shadow_depth_bias                = 0.0f;
    float shadow_normal_bias               = 0.0f;
    std::uint32_t num_cascades             = 0;
    float cascades_overlap_proportion      = 0.0f;
    std::uint32_t depth_texture_base_index = 0;
    std::uint32_t decal_index              = 0;
    float sun_disk_angular_size            = 0.0f;
    float sun_disk_intensity               = 0.0f;
    /** @brief Padding to the shader type's 16-byte-aligned size. Epix's glm
     *  types are unaligned, so C++ lays this struct out as 388 bytes and does
     *  not round the tail up; the shader type is 400, and without these twelve
     *  bytes every field of `GpuLights` after the first light would sit at the
     *  wrong offset. */
    std::array<std::uint32_t, 3> padding{0u, 0u, 0u};
};
static_assert(sizeof(GpuDirectionalLight) == 400, "the shader type is 400 bytes: 388 plus tail padding to 16");

/** @brief The per-view light uniform, which `mesh_view_bindings.wgsl` binds at
 *  group 0 binding 1 (Bevy `GpuLights`, `light.rs:160-174`).
 *
 *  As with the cascade and the light above, the trailing padding brings the
 *  struct to its shader size: 4060 bytes of fields, padded to 4064. */
EPIX_EXPORT struct GpuLights {
    std::array<GpuDirectionalLight, MAX_DIRECTIONAL_LIGHTS> directional_lights{};
    glm::vec4 ambient_color{1.0f, 1.0f, 1.0f, 1.0f};
    /** @brief x/y/z are the cluster dimensions and w the cluster count. */
    glm::uvec4 cluster_dimensions{0, 0, 0, 0};
    /** @brief xy are `cluster_dimensions.xy / vec2(view width, view height)`, z
     *  is `cluster_dimensions.z / log(far / near)` and w is
     *  `cluster_dimensions.z * log(near) / log(far / near)`. */
    glm::vec4 cluster_factors{0.0f};
    std::uint32_t n_directional_lights = 0;
    /** @brief Offset from a spot light's light index to its shadow map index. */
    std::int32_t spot_light_shadowmap_offset               = 0;
    std::uint32_t ambient_light_affects_lightmapped_meshes = 0;
    /** @brief Padding to the shader type's 16-byte-aligned size. */
    std::uint32_t padding = 0;
};
static_assert(sizeof(GpuLights) == 4064, "the shader type is 4064 bytes: 4060 of fields, padded to 16");
/** @brief The default-constructed light uniform, as Bevy's `GpuLights` cannot be
 *  `Default` (its arrays are not) - the shader sees zeroed lights until
 *  `prepare_lights` fills them. */
EPIX_EXPORT inline GpuLights default_gpu_lights() { return GpuLights{}; }

}  // namespace epix::pbr

namespace epix::render::render_resource {
/** @brief Bevy's `ShaderType` derive writes the struct's own layout for these,
 *  and the padding above is what makes that layout the shader's. */
template <>
struct ShaderTypeInfo<::epix::pbr::GpuDirectionalCascade> : RawShaderType<::epix::pbr::GpuDirectionalCascade> {};
template <>
struct ShaderTypeInfo<::epix::pbr::GpuDirectionalLight> : RawShaderType<::epix::pbr::GpuDirectionalLight> {};
template <>
struct ShaderTypeInfo<::epix::pbr::GpuLights> : RawShaderType<::epix::pbr::GpuLights> {};
}  // namespace epix::render::render_resource

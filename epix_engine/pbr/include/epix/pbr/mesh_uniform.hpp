#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief A mesh instance's transform, in the render world (Bevy `MeshTransforms`,
 *  `bevy_pbr/src/render/mesh.rs:448-453`).
 *
 *  Bevy stores glam `Affine3` values, which are 3x4; Epix's convention is a full
 *  `glm::mat4`, and the uniform below takes its first three columns exactly as
 *  Bevy takes the affine's. The previous transform is kept so motion vectors can
 *  be derived without re-reading the main world. */
EPIX_EXPORT struct MeshTransforms {
    glm::mat4 world_from_local{1.0f};
    glm::mat4 previous_world_from_local{1.0f};
    /** @brief A `RenderMeshInstanceFlags` bit set, kept as a plain integer because
     *  the uniform carries it as one. */
    std::uint32_t flags = 0;
};

/** @brief The per-instance uniform a mesh's vertex shader reads (Bevy
 *  `MeshUniform`, `bevy_pbr/src/render/mesh.rs:455-494`).
 *
 *  Both transforms are stored as three `vec4`s - the affine's columns, with the
 *  translation in the third - which is what Bevy means by "transposed to 3x4".
 *
 *  `local_from_world_transpose` is the inverse transpose of the rotation and scale,
 *  packed to save space: a 3x3 matrix needs nine floats, and they are laid out as
 *  two `vec4`s and a trailing `f32`, because a `vec4`-aligned member cannot straddle
 *  one. The packing is described where the shader reads it, and getting it wrong
 *  changes normals rather than positions, which is why the offsets below are
 *  asserted rather than assumed.
 *
 *  `lightmap_uv_rect` packs four 16-bit normalized UVs into two 32-bit words;
 *  `material_and_lightmap_bind_group_slot` packs the material index in the low 16
 *  bits and the lightmap index in the high 16. */
EPIX_EXPORT struct MeshUniform {
    std::array<glm::vec4, 3> world_from_local{};
    std::array<glm::vec4, 3> previous_world_from_local{};
    std::array<glm::vec4, 2> local_from_world_transpose_a{};
    float local_from_world_transpose_b = 0.0f;
    std::uint32_t flags                = 0;
    glm::uvec2 lightmap_uv_rect{0u, 0u};
    std::uint32_t first_vertex_index = 0;
    /** @brief The skin index, or `UINT32_MAX` when the mesh has no skin. */
    std::uint32_t current_skin_index = 0xFFFFFFFFu;
    /** @brief Material index in the low 16 bits, lightmap index in the high 16. */
    std::uint32_t material_and_lightmap_bind_group_slot = 0;
    std::uint32_t tag                                   = 0;
    /** @brief Bevy's trailing word. */
    std::uint32_t pad = 0;
    /** @brief Explicit tail padding. Bevy's `ShaderType` derive rounds the struct
     *  up to its own alignment - 16, because it contains `vec4`s - but glm's types
     *  are not 16-byte aligned unless `GLM_FORCE_ALIGNED_GENTYPES` is set, so the
     *  remainder is spelled out here to reach the size the shader reads. */
    std::array<std::uint32_t, 3> tail_padding{};
};

// The shader's uniform requires 16-byte alignment, which glm's vec4 members give
// the struct - so these are the values the GPU sees, not just the CPU's.
static_assert(sizeof(glm::vec4) == 16);
static_assert(sizeof(MeshUniform) == 176, "MeshUniform must be 16-byte aligned in size");
static_assert(offsetof(MeshUniform, previous_world_from_local) == 48);
static_assert(offsetof(MeshUniform, local_from_world_transpose_a) == 96);
static_assert(offsetof(MeshUniform, local_from_world_transpose_b) == 128);
static_assert(offsetof(MeshUniform, flags) == 132);
static_assert(offsetof(MeshUniform, lightmap_uv_rect) == 136);
static_assert(offsetof(MeshUniform, first_vertex_index) == 144);
static_assert(offsetof(MeshUniform, current_skin_index) == 148);
static_assert(offsetof(MeshUniform, material_and_lightmap_bind_group_slot) == 152);
static_assert(offsetof(MeshUniform, tag) == 156);
static_assert(offsetof(MeshUniform, pad) == 160);

}  // namespace epix::pbr

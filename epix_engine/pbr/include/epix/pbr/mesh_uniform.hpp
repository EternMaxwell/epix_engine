#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
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
    /** @brief A `MeshFlags` bit set, kept as a plain integer because the uniform
     *  carries it as one (Bevy fills it from `MeshFlags::bits()`). */
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

/** @brief Information that has to be transferred from CPU to GPU in order to
 *  produce the full `MeshUniform` (Bevy `MeshInputUniform`,
 *  `bevy_pbr/src/render/mesh.rs:500-560`).
 *
 *  A subset of the uniform above: the GPU's injection pass reads this and builds
 *  the full `MeshUniform`, which is what lets the CPU skip rebuilding one per mesh
 *  per frame. Field order is the shader's.
 *
 *  Bevy derives `Default`, so a freshly built input uniform is all zeros; the
 *  `u32::MAX` sentinels the comments below mention are written by the producer
 *  (`MeshInputUniform` in the extraction system), not by the default. */
EPIX_EXPORT struct MeshInputUniform {
    /** @brief Affine 4x3 matrix transposed to 3x4. */
    std::array<glm::vec4, 3> world_from_local{};
    /** @brief Four 16-bit unsigned normalized UV values packed into two words;
     *  layout as in `MeshUniform::lightmap_uv_rect`. */
    glm::uvec2 lightmap_uv_rect{0u, 0u};
    /** @brief Various `MeshFlags`. */
    std::uint32_t flags = 0;
    /** @brief The index of this mesh's input uniform in the previous frame's
     *  buffer, `UINT32_MAX` when it has none (used for TAA). */
    std::uint32_t previous_input_index = 0;
    /** @brief The offset of this mesh's first vertex in the vertex buffer. */
    std::uint32_t first_vertex_index = 0;
    /** @brief The offset of this mesh's first index in the index buffer; ignored
     *  for a non-indexed mesh. */
    std::uint32_t first_index_index = 0;
    /** @brief For an indexed mesh the number of indices, for a non-indexed one
     *  the number of vertices. */
    std::uint32_t index_count = 0;
    /** @brief The current skin index, or `UINT32_MAX` when there is no skin. */
    std::uint32_t current_skin_index = 0;
    /** @brief Material index in the low 16 bits, lightmap index in the high 16. */
    std::uint32_t material_and_lightmap_bind_group_slot = 0;
    /** @brief The frame this input uniform was built on, which is how the
     *  injection pass validates the previous transform and skin. */
    std::uint32_t timestamp = 0;
    /** @brief User supplied tag to identify this mesh instance. */
    std::uint32_t tag = 0;
    /** @brief Bevy's trailing word. */
    std::uint32_t pad = 0;
};

// The shader's contract, as for `MeshUniform` above: three `vec4`s, one `uvec2`
// and ten words. Unlike `MeshUniform` this needs no tail padding - 96 is already
// a multiple of the 16-byte alignment Bevy's `ShaderType` derive gives it.
static_assert(sizeof(MeshInputUniform) == 96, "MeshInputUniform must match the shader's size");
static_assert(offsetof(MeshInputUniform, lightmap_uv_rect) == 48);
static_assert(offsetof(MeshInputUniform, flags) == 56);
static_assert(offsetof(MeshInputUniform, previous_input_index) == 60);
static_assert(offsetof(MeshInputUniform, first_vertex_index) == 64);
static_assert(offsetof(MeshInputUniform, first_index_index) == 68);
static_assert(offsetof(MeshInputUniform, index_count) == 72);
static_assert(offsetof(MeshInputUniform, current_skin_index) == 76);
static_assert(offsetof(MeshInputUniform, material_and_lightmap_bind_group_slot) == 80);
static_assert(offsetof(MeshInputUniform, timestamp) == 84);
static_assert(offsetof(MeshInputUniform, tag) == 88);
static_assert(offsetof(MeshInputUniform, pad) == 92);

/** @brief Various flags and tightly-packed values on a mesh (Bevy `MeshFlags`,
 *  `bevy_pbr/src/render/mesh.rs:619-643`).
 *
 *  Flags grow from the top bit down; other values grow from the bottom bit up.
 *  The bits must match `bevy_pbr/src/render/mesh_types.wgsl`, which is why each
 *  one is named here rather than written at its use site. */
EPIX_EXPORT struct MeshFlags {
    enum Bits : std::uint32_t {
        NONE                          = 0,
        /** @brief Bitmask for the 16-bit index into the LOD array; `u16::MAX`
         *  when the mesh has no LOD. */
        LOD_INDEX_MASK                = (1u << 16) - 1u,
        /** @brief Disables frustum culling for this mesh (the
         *  `NoFrustumCulling` component). */
        NO_FRUSTUM_CULLING            = 1u << 28,
        SHADOW_RECEIVER               = 1u << 29,
        TRANSMITTED_SHADOW_RECEIVER   = 1u << 30,
        /** @brief The sign of the 3x3 model matrix's determinant: set when the
         *  sign is positive, clear otherwise. */
        SIGN_DETERMINANT_MODEL_3X3    = 1u << 31,
        UNINITIALIZED                 = 0xFFFFFFFFu,
    };

    /** @brief The first bit of the LOD index (Bevy `MeshFlags::LOD_INDEX_SHIFT`,
     *  `:679`). */
    static constexpr std::uint32_t LOD_INDEX_SHIFT = 0;

    std::uint32_t bits = NONE;

    constexpr bool contains(Bits flag) const noexcept { return (bits & static_cast<std::uint32_t>(flag)) != 0; }
    constexpr void insert(Bits flag) noexcept { bits |= static_cast<std::uint32_t>(flag); }
    constexpr void remove(Bits flag) noexcept { bits &= ~static_cast<std::uint32_t>(flag); }

    bool operator==(const MeshFlags&) const = default;

    /** @brief Bevy `MeshFlags::from_components` (`render/mesh.rs:645-676`): the
     *  shadow-receiver flag is on unless the mesh opted out, the frustum-culling
     *  and transmitted-receiver flags come straight from their components, the
     *  determinant flag from the model matrix's orientation, and the low 16 bits
     *  carry the LOD index or its `u16::MAX` sentinel.
     *
     *  Bevy takes an `Option<NonMaxU16>`, whose type guarantees the sentinel
     *  cannot collide with a real index; Epix has no non-max wrapper, so the
     *  optional carries its index and the invariant is documented (the same shape
     *  `mesh_instance.hpp` records for `Option<NonMaxU32>`). */
    static MeshFlags from_components(const ::epix::transform::GlobalTransform& transform,
                                     std::optional<std::uint16_t> lod_index,
                                     bool no_frustum_culling,
                                     bool not_shadow_receiver,
                                     bool transmitted_receiver) {
        MeshFlags flags = not_shadow_receiver ? MeshFlags{} : MeshFlags{SHADOW_RECEIVER};
        if (no_frustum_culling) flags.insert(NO_FRUSTUM_CULLING);
        if (transmitted_receiver) flags.insert(TRANSMITTED_SHADOW_RECEIVER);
        // `is_sign_positive` is the sign bit's state, so a negative zero is
        // positive and a NaN follows its own sign bit - `signbit` is the
        // counterpart, not `> 0`.
        if (!std::signbit(glm::determinant(glm::mat3(transform.matrix)))) flags.insert(SIGN_DETERMINANT_MODEL_3X3);

        const std::uint16_t index = lod_index.value_or(std::numeric_limits<std::uint16_t>::max());
        flags.bits |= static_cast<std::uint32_t>(index) << LOD_INDEX_SHIFT;
        return flags;
    }
};

}  // namespace epix::pbr

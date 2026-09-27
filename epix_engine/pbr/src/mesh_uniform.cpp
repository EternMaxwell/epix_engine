#include <epix/pbr/mesh_uniform.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace epix::pbr {

/** @brief The affine's 3x3 transposed to three `vec4`s, the translation in each
 *  one's fourth component (Bevy `Affine3::to_transpose`,
 *  `bevy_math/src/affine3.rs:20-27`).
 *
 *  Bevy can put this on `Affine3`; C++ cannot add a member to `glm::mat4`, and
 *  Epix's `GlobalTransform` is matrix-only, so the counterpart lives beside its
 *  only consumer. A column-major `glm::mat4` stores the affine's columns in `[0]`,
 *  `[1]` and `[2]` and the translation in `[3]`, so row `i` of the 3x3 is
 *  `(m[0][i], m[1][i], m[2][i])`. */
EPIX_EXPORT inline std::array<glm::vec4, 3> to_transpose(const glm::mat4& world_from_local) {
    return {
        glm::vec4(world_from_local[0][0], world_from_local[1][0], world_from_local[2][0], world_from_local[3][0]),
        glm::vec4(world_from_local[0][1], world_from_local[1][1], world_from_local[2][1], world_from_local[3][1]),
        glm::vec4(world_from_local[0][2], world_from_local[1][2], world_from_local[2][2], world_from_local[3][2]),
    };
}

/** @brief The inverse transpose of the affine's 3x3, packed for the GPU (Bevy
 *  `Affine3::inverse_transpose_3x3`, `bevy_math/src/affine3.rs:29-44`).
 *
 *  A 3x3 needs nine floats and cannot straddle a `vec4`, so Bevy packs them as
 *  `[0].xyz, [1].x` then `[1].yz, [2].xy` then `[2].z` - the layout the uniform's
 *  two `vec4`s and trailing float take. Normalising normals by the inverse
 *  transpose is what keeps them correct under non-uniform scale. */
EPIX_EXPORT inline std::pair<std::array<glm::vec4, 2>, float> inverse_transpose_3x3(const glm::mat4& world_from_local) {
    // The affine's inverse; its 3x3 part is the inverse of the 3x3 because the
    // fourth column does not affect it. Transposed, because that is what
    // transforms normals.
    const glm::mat3 inverse_transpose = glm::transpose(glm::mat3(glm::inverse(world_from_local)));
    return {
        std::array<glm::vec4, 2>{
            glm::vec4(inverse_transpose[0][0], inverse_transpose[0][1], inverse_transpose[0][2],
                      inverse_transpose[1][0]),
            glm::vec4(inverse_transpose[1][1], inverse_transpose[1][2], inverse_transpose[2][0],
                      inverse_transpose[2][1]),
        },
        inverse_transpose[2][2],
    };
}

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

MeshUniform MeshUniform::make(const MeshTransforms& mesh_transforms,
                            std::uint32_t first_vertex_index,
                            MaterialBindGroupSlot material_bind_group_slot,
                            std::optional<std::pair<LightmapSlotIndex, ::epix::image::Rect>> maybe_lightmap,
                            std::optional<std::uint32_t> current_skin_index,
                            std::optional<std::uint32_t> tag) {        const auto [local_from_world_transpose_a, local_from_world_transpose_b] =
            inverse_transpose_3x3(mesh_transforms.world_from_local);
        const std::uint32_t lightmap_bind_group_slot =
            maybe_lightmap ? static_cast<std::uint32_t>(maybe_lightmap->first.index) : 0xFFFFu;

        MeshUniform uniform{};
        uniform.world_from_local          = to_transpose(mesh_transforms.world_from_local);
        uniform.previous_world_from_local = to_transpose(mesh_transforms.previous_world_from_local);
        uniform.lightmap_uv_rect          = pack_lightmap_uv_rect(
            maybe_lightmap ? std::optional<::epix::image::Rect>{maybe_lightmap->second} : std::nullopt);
        uniform.local_from_world_transpose_a = local_from_world_transpose_a;
        uniform.local_from_world_transpose_b = local_from_world_transpose_b;
        uniform.flags                        = mesh_transforms.flags;
        uniform.first_vertex_index           = first_vertex_index;
        uniform.current_skin_index           = current_skin_index.value_or(0xFFFFFFFFu);
        uniform.material_and_lightmap_bind_group_slot =
            material_bind_group_slot.slot | (lightmap_bind_group_slot << 16);
        uniform.tag = tag.value_or(0u);
        uniform.pad = 0;
        return uniform;
    }

}  // namespace epix::pbr

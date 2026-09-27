#include <epix/pbr/mesh_uniform.hpp>

#include <cstdint>
#include <optional>
#include <utility>

namespace epix::pbr {

EPIX_EXPORT EPIX_API std::array<glm::vec4, 3> to_transpose(const glm::mat4& world_from_local) {
    return {
        glm::vec4(world_from_local[0][0], world_from_local[1][0], world_from_local[2][0], world_from_local[3][0]),
        glm::vec4(world_from_local[0][1], world_from_local[1][1], world_from_local[2][1], world_from_local[3][1]),
        glm::vec4(world_from_local[0][2], world_from_local[1][2], world_from_local[2][2], world_from_local[3][2]),
    };
}

EPIX_EXPORT EPIX_API std::pair<std::array<glm::vec4, 2>, float> inverse_transpose_3x3(const glm::mat4& world_from_local) {
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

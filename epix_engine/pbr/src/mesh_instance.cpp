#include <epix/pbr/mesh_instance.hpp>

#include <cstdint>
#include <optional>

namespace epix::pbr {

std::optional<std::uint32_t> remove_mesh_input_uniform(
    render::sync_world::MainEntity entity,
    render::sync_world::MainEntityHashMap<RenderMeshInstanceGpu>& render_mesh_instances,
    render::batching::InstanceInputUniformBuffer<MeshInputUniform>& current_input_buffer) {
    const auto found = render_mesh_instances.find(entity);
    if (found == render_mesh_instances.end()) return std::nullopt;

    const std::uint32_t removed_uniform_index = found->second.current_uniform_index;
    render_mesh_instances.erase(found);
    current_input_buffer.remove(removed_uniform_index);
    return removed_uniform_index;
}

std::optional<std::uint32_t> RenderMeshInstanceGpuBuilder::update(
    render::sync_world::MainEntity entity,
    render::sync_world::MainEntityHashMap<RenderMeshInstanceGpu>& render_mesh_instances,
    render::batching::InstanceInputUniformBuffer<MeshInputUniform>& current_input_buffer,
    render::batching::InstanceInputUniformBuffer<MeshInputUniform>& previous_input_buffer,
    const render::mesh::MeshAllocator& mesh_allocator, const RenderMaterialInstances& mesh_material_ids,
    const RenderMaterialBindings& render_material_bindings, const RenderLightmaps& render_lightmaps,
    const SkinUniforms& skin_uniforms, render::FrameCount timestamp,
    MeshesToReextractNextFrame& meshes_to_reextract_next_frame) {
    const auto vertex_slice = mesh_allocator.mesh_vertex_slice(shared.mesh_asset_id);
    const std::uint32_t first_vertex_index = vertex_slice ? vertex_slice->range.first : 0u;
    const std::uint32_t vertex_count = vertex_slice ? vertex_slice->range.second - vertex_slice->range.first : 0u;

    const auto index_slice = mesh_allocator.mesh_index_slice(shared.mesh_asset_id);
    const bool mesh_is_indexed = index_slice.has_value();
    const std::uint32_t first_index_index = index_slice ? index_slice->range.first : 0u;
    const std::uint32_t index_count = index_slice ? index_slice->range.second - index_slice->range.first : 0u;

    // Bevy sends `u32::MAX` where a mesh has no skin, so the shader can tell the
    // two apart without a second flag.
    const std::uint32_t current_skin_index = skin_uniforms.skin_byte_offset(entity)
                                                 .transform([](const SkinByteOffset& offset) { return offset.index(); })
                                                 .value_or(std::numeric_limits<std::uint32_t>::max());

    // A material that has not been prepared is not guessed at: the mesh is queued
    // for re-extraction next frame and nothing is written this one.
    const assets::UntypedAssetId mesh_material = mesh_material_ids.mesh_material(entity);
    MaterialBindingId mesh_material_binding_id{};
    if (mesh_material != DUMMY_MESH_MATERIAL) {
        const auto binding = render_material_bindings.get(mesh_material);
        if (!binding) {
            meshes_to_reextract_next_frame->insert(entity);
            return std::nullopt;
        }
        mesh_material_binding_id = *binding;
    }
    shared.material_bindings_index = mesh_material_binding_id;

    const auto lightmap_found = render_lightmaps.render_lightmaps.find(entity);
    const RenderLightmap* lightmap =
        lightmap_found == render_lightmaps.render_lightmaps.end() ? nullptr : &lightmap_found->second;
    const std::uint16_t lightmap_slot =
        lightmap != nullptr ? static_cast<std::uint16_t>(lightmap->slot_index.index) : std::numeric_limits<std::uint16_t>::max();
    shared.lightmap_slab_index = lightmap != nullptr ? std::optional{lightmap->slab_index} : std::nullopt;

    MeshInputUniform mesh_input_uniform{
        .world_from_local                       = to_transpose(world_from_local),
        .lightmap_uv_rect                       = lightmap_uv_rect,
        .flags                                  = mesh_flags.bits,
        .previous_input_index                   = std::numeric_limits<std::uint32_t>::max(),
        .first_vertex_index                     = first_vertex_index,
        .first_index_index                      = first_index_index,
        .index_count                            = mesh_is_indexed ? index_count : vertex_count,
        .current_skin_index                     = current_skin_index,
        .material_and_lightmap_bind_group_slot  = shared.material_bindings_index.slot.slot |
                                                  (static_cast<std::uint32_t>(lightmap_slot) << 16),
        .timestamp                              = timestamp.count,
        .tag                                    = shared.tag,
    };

    // The instance's centre in world space: the local centre through the model
    // matrix, whose third column carries the translation.
    const glm::vec3 center = glm::mat3(world_from_local) * shared.center + glm::vec3(world_from_local[3]);

    std::uint32_t current_uniform_index = 0;
    const auto found                    = render_mesh_instances.find(entity);
    if (found != render_mesh_instances.end()) {
        // Already present: keep its slot, and hand the uniform it held to the
        // previous frame's buffer, which is what motion vectors read.
        current_uniform_index = found->second.current_uniform_index;
        const MeshInputUniform previous_mesh_input_uniform = current_input_buffer.get_unchecked(current_uniform_index);
        mesh_input_uniform.previous_input_index            = previous_input_buffer.add(previous_mesh_input_uniform);
        current_input_buffer.set(current_uniform_index, mesh_input_uniform);
        found->second = RenderMeshInstanceGpu{
            .shared = shared, .center = center, .current_uniform_index = current_uniform_index};
    } else {
        current_uniform_index = current_input_buffer.add(mesh_input_uniform);
        render_mesh_instances.emplace(
            entity, RenderMeshInstanceGpu{
                        .shared = shared, .center = center, .current_uniform_index = current_uniform_index});
    }

    return current_uniform_index;
}

}  // namespace epix::pbr

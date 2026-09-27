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

}  // namespace epix::pbr

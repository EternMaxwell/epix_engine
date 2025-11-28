#include "epix/mesh/gpumesh.hpp"

using namespace epix;
using namespace epix::mesh;

GPUMesh GPUMesh::create_from_mesh(const Mesh& mesh, nvrhi::DeviceHandle device) {
    GPUMesh gpu_mesh;
    gpu_mesh.update_from_mesh(mesh, device);
    return std::move(gpu_mesh);
}
void GPUMesh::update_from_mesh(const Mesh& mesh, nvrhi::DeviceHandle device) {
    _primitive_type     = mesh.get_primitive_type();
    _attributes         = mesh.attribute_layout();
    size_t vertex_count = mesh.count_vertices();
    size_t total_bytes  = std::ranges::fold_left(mesh.iter_attributes(), size_t(0),
                                                 [vertex_count](size_t acc, const MeshAttributeData& attr_data) {
                                                    return acc + attr_data.data.type_info().size * vertex_count;
                                                });
    nvrhi::CommandListHandle command_list =
        device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    command_list->open();
    if (total_bytes != 0) {
        if (total_bytes > (_combined_buffer ? _combined_buffer->getDesc().byteSize : 0)) {
            _combined_buffer = device->createBuffer(nvrhi::BufferDesc()
                                                        .setByteSize(total_bytes)
                                                        .setDebugName("GPUMesh::combined_buffer")
                                                        .setIsVertexBuffer(true)
                                                        .setKeepInitialState(true)
                                                        .setInitialState(nvrhi::ResourceStates::VertexBuffer));
        }
        size_t offset = 0;
        for (auto&& [slot, pack] : mesh.iter_attributes() | std::views::enumerate) {
            auto&& [attribute, data] = pack;
            command_list->writeBuffer(_combined_buffer, data.cdata(), data.type_info().size * vertex_count, offset);
            _attribute_bindings.push_back({(uint32_t)(slot), offset});
            offset += data.type_info().size * vertex_count;
        }
    }
    mesh.get_indices().and_then([&](const MeshIndices& indices) -> std::optional<bool> {
        size_t byte_size = indices.data.type_info().size * indices.data.size();
        if (byte_size > (_index_buffer ? _index_buffer->getDesc().byteSize : 0)) {
            _index_buffer = device->createBuffer(nvrhi::BufferDesc()
                                                     .setByteSize(byte_size)
                                                     .setDebugName("GPUMesh::index_buffer")
                                                     .setIsIndexBuffer(true)
                                                     .setKeepInitialState(true)
                                                     .setInitialState(nvrhi::ResourceStates::IndexBuffer));
        }
        command_list->writeBuffer(_index_buffer, indices.data.cdata(), byte_size, 0);
        _index_binding = IndexBindingInfo{indices.is_u16() ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT, 0};
        return true;
    });
    command_list->close();
    device->executeCommandList(command_list);
    _vertex_count = mesh.get_indices()
                        .transform([](const MeshIndices& indices) { return indices.data.size(); })
                        .value_or(vertex_count);
}
void GPUMesh::bind_state(nvrhi::GraphicsState& state) const {
    std::ranges::for_each(_attribute_bindings, [&](const VertexBindingInfo& binding) {
        state.addVertexBuffer({_combined_buffer, binding.slot, binding.offset});
    });
    if (is_indexed()) {
        state.setIndexBuffer({_index_buffer, _index_binding->format, _index_binding->offset});
    }
}
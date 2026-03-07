module epix.mesh;

using namespace mesh;

GPUMesh GPUMesh::create_from_mesh(const Mesh& mesh, const wgpu::Device& device) {
    GPUMesh gpu_mesh;
    gpu_mesh.update_from_mesh(mesh, device);
    return gpu_mesh;
}
void GPUMesh::update_from_mesh(const Mesh& mesh, const wgpu::Device& device) {
    _primitive_type = mesh.get_primitive_type();
    _attributes     = mesh.attribute_layout();
    _attribute_bindings.clear();
    _index_binding.reset();
    size_t vertex_count = mesh.count_vertices();
    size_t total_bytes  = std::ranges::fold_left(mesh.iter_attributes(), size_t(0),
                                                 [vertex_count](size_t acc, const MeshAttributeData& attr_data) {
                                                    return acc + attr_data.data.type_info().size * vertex_count;
                                                 });
    auto queue          = device.getQueue();
    if (total_bytes != 0) {
        if (total_bytes > (_combined_buffer ? _combined_buffer.getSize() : 0)) {
            _combined_buffer =
                device.createBuffer(wgpu::BufferDescriptor()
                                        .setSize(total_bytes)
                                        .setLabel("GPUMesh::combined_buffer")
                                        .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        }
        size_t offset = 0;
        for (auto&& attribute_data : mesh.iter_attributes()) {
            auto&& [attribute, data] = attribute_data;
            auto byte_size           = data.type_info().size * vertex_count;
            queue.writeBuffer(_combined_buffer, offset, data.cdata(), byte_size);
            _attribute_bindings.push_back({attribute.slot, offset, byte_size});
            offset += byte_size;
        }
    }
    mesh.get_indices().and_then([&](const MeshIndices& indices) -> std::optional<bool> {
        size_t byte_size = indices.data.type_info().size * indices.data.size();
        if (byte_size > (_index_buffer ? _index_buffer.getSize() : 0)) {
            _index_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                    .setSize(byte_size)
                                                    .setLabel("GPUMesh::index_buffer")
                                                    .setUsage(wgpu::BufferUsage::eIndex | wgpu::BufferUsage::eCopyDst));
        }
        queue.writeBuffer(_index_buffer, 0, indices.data.cdata(), byte_size);
        _index_binding =
            IndexBindingInfo{indices.is_u16() ? wgpu::IndexFormat::eUint16 : wgpu::IndexFormat::eUint32, 0, byte_size};
        return true;
    });
    _vertex_count =
        mesh.get_indices().transform([](const MeshIndices& indices) { return indices.size(); }).value_or(vertex_count);
}
void GPUMesh::bind_to(const wgpu::RenderPassEncoder& encoder) const {
    std::ranges::for_each(_attribute_bindings, [&](const VertexBindingInfo& binding) {
        encoder.setVertexBuffer(binding.slot, _combined_buffer, binding.offset, binding.size);
    });
    if (is_indexed()) {
        encoder.setIndexBuffer(_index_buffer, _index_binding->format, _index_binding->offset, _index_binding->size);
    }
}
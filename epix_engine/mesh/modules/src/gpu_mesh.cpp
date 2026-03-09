module epix.mesh;

using namespace mesh;

namespace {
constexpr std::size_t kBufferWriteAlignment = 4;

std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    auto remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

struct UploadBufferView {
    std::optional<core::untyped_vector> owned;
    const void* data = nullptr;
    std::size_t size = 0;
};

UploadBufferView make_upload_buffer_view(const core::untyped_vector& data) {
    UploadBufferView view;
    auto element_size   = data.type_info().size;
    auto used_bytes     = element_size * data.size();
    auto required_bytes = align_up(used_bytes, kBufferWriteAlignment);
    auto capacity_bytes = element_size * data.capacity();

    if (required_bytes == used_bytes) {
        view.data = data.cdata();
        view.size = used_bytes;
        return view;
    }

    if (capacity_bytes >= required_bytes && capacity_bytes % kBufferWriteAlignment == 0) {
        view.data = data.cdata();
        view.size = capacity_bytes;
        return view;
    }

    auto reserved_elements = (required_bytes + element_size - 1) / element_size;
    view.owned.emplace(data.clone());
    view.owned->reserve(reserved_elements);
    view.data = view.owned->cdata();
    view.size = view.owned->capacity() * element_size;
    return view;
}
}  // namespace

GPUMesh GPUMesh::create_from_mesh(const Mesh& mesh, const wgpu::Device& device) {
    GPUMesh gpu_mesh;
    wgpu::Limits limits;
    device.getLimits(&limits);
    gpu_mesh.update_from_mesh(mesh, device, limits);
    return gpu_mesh;
}
GPUMesh GPUMesh::create_from_mesh(const Mesh& mesh, const wgpu::Device& device, const wgpu::Limits& limits) {
    GPUMesh gpu_mesh;
    gpu_mesh.update_from_mesh(mesh, device, limits);
    return gpu_mesh;
}
void GPUMesh::update_from_mesh(const Mesh& mesh, const wgpu::Device& device, const wgpu::Limits& limits) {
    _primitive_type = mesh.get_primitive_type();
    _attributes     = mesh.attribute_layout();
    _attribute_bindings.clear();
    _index_binding.reset();
    size_t vertex_count = mesh.count_vertices();
    size_t total_bytes  = std::ranges::fold_left(mesh.iter_attributes(), size_t(0),
                                                 [vertex_count](size_t acc, const MeshAttributeData& attr_data) {
                                                    auto byte_size  = attr_data.data.type_info().size * vertex_count;
                                                    auto write_size = align_up(byte_size, kBufferWriteAlignment);
                                                    return align_up(acc, kBufferWriteAlignment) + write_size;
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
            auto upload_view         = make_upload_buffer_view(data);
            offset                   = align_up(offset, kBufferWriteAlignment);
            auto byte_size           = data.type_info().size * vertex_count;
            queue.writeBuffer(_combined_buffer, offset, upload_view.data, upload_view.size);
            _attribute_bindings.push_back({attribute.slot, offset, byte_size});
            offset += upload_view.size;
        }
    }
    mesh.get_indices().and_then([&](const MeshIndices& indices) -> std::optional<bool> {
        auto upload_view      = make_upload_buffer_view(indices.data);
        size_t byte_size      = indices.data.type_info().size * indices.data.size();
        size_t reserved_bytes = upload_view.size;
        if (reserved_bytes > (_index_buffer ? _index_buffer.getSize() : 0)) {
            _index_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                    .setSize(reserved_bytes)
                                                    .setLabel("GPUMesh::index_buffer")
                                                    .setUsage(wgpu::BufferUsage::eIndex | wgpu::BufferUsage::eCopyDst));
        }
        queue.writeBuffer(_index_buffer, 0, upload_view.data, upload_view.size);
        _index_binding =
            IndexBindingInfo{indices.is_u16() ? wgpu::IndexFormat::eUint16 : wgpu::IndexFormat::eUint32, 0, byte_size};
        return true;
    });
    _vertex_count =
        mesh.get_indices().transform([](const MeshIndices& indices) { return indices.size(); }).value_or(vertex_count);
}
void GPUMesh::bind_to(const wgpu::RenderPassEncoder& encoder) const {
    for (std::uint32_t buffer_index = 0; buffer_index < _attribute_bindings.size(); ++buffer_index) {
        auto&& binding = _attribute_bindings[buffer_index];
        encoder.setVertexBuffer(buffer_index, _combined_buffer, binding.offset, binding.size);
    }
    if (is_indexed()) {
        encoder.setIndexBuffer(_index_buffer, _index_binding->format, _index_binding->offset, _index_binding->size);
    }
}
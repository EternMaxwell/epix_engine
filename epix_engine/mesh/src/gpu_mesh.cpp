

#include <cstring>

#include <epix/mesh.hpp>

using namespace epix;
using namespace epix::mesh;

std::uint32_t epix::mesh::vertex_array_stride(const Mesh& mesh) {
    std::uint32_t stride = 0;
    for (const auto& data : mesh.iter_attributes()) {
        stride += vertex_format_size(data.attribute.format);
    }
    return stride;
}

std::vector<std::uint8_t> epix::mesh::packed_vertex_bytes(const Mesh& mesh) {
    const auto count   = mesh.count_vertices();
    const auto stride  = vertex_array_stride(mesh);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(stride) * count, 0);
    std::uint32_t attr_offset = 0;
    for (const auto& data : mesh.iter_attributes()) {
        const auto elem_size = data.data.type_info().size;
        const auto* base     = static_cast<const std::uint8_t*>(data.data.cdata());
        for (std::size_t v = 0; v < count; ++v) {
            std::memcpy(out.data() + v * stride + attr_offset, base + v * elem_size, elem_size);
        }
        attr_offset += static_cast<std::uint32_t>(elem_size);
    }
    return out;
}

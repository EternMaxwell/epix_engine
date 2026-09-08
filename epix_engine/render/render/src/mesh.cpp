

#include <cstring>
#include <epix/mesh/mesh.hpp>
#include <epix/render/mesh/render_asset.hpp>

using namespace epix;
using namespace epix::render::mesh;

std::uint32_t epix::render::mesh::vertex_array_stride(const epix::mesh::Mesh& mesh) {
    std::uint32_t stride = 0;
    for (const auto& [attribute, values] : mesh.attributes()) {
        stride += epix::mesh::vertex_format_size(attribute.format);
    }
    return stride;
}

std::vector<std::uint8_t> epix::render::mesh::packed_vertex_bytes(const epix::mesh::Mesh& mesh) {
    const auto count  = mesh.count_vertices();
    const auto stride = vertex_array_stride(mesh);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(stride) * count, 0);
    std::uint32_t attr_offset = 0;
    for (const auto& [attribute, values] : mesh.attributes()) {
        const auto elem_size = values.type_info().size;
        const auto* base     = static_cast<const std::uint8_t*>(values.cdata());
        for (std::size_t v = 0; v < count; ++v) {
            std::memcpy(out.data() + v * stride + attr_offset, base + v * elem_size, elem_size);
        }
        attr_offset += static_cast<std::uint32_t>(elem_size);
    }
    return out;
}

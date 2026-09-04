#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/render.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/mesh/mesh.hpp>
#include <epix/mesh/mesh_allocator.hpp>
#include <epix/mesh/render_mesh.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>

namespace epix::mesh {
/** @brief Per-vertex byte stride of a mesh's packed vertex data (Bevy
 * `MeshVertexBufferLayout::array_stride`): the sum of all attribute sizes. */
EPIX_EXPORT std::uint32_t vertex_array_stride(const Mesh& mesh);
/** @brief Pack a mesh's per-attribute arrays into Bevy's interleaved per-vertex
 * vertex buffer (`Mesh::write_packed_vertex_buffer_data`): each vertex holds its
 * attributes in slot order. */
EPIX_EXPORT std::vector<std::uint8_t> packed_vertex_bytes(const Mesh& mesh);
}  // namespace epix::mesh

// Bevy 0.18 `RenderAsset for RenderMesh` (mesh/mod.rs:124-202): the render
// world representation of a Mesh is lightweight metadata (vertex count, index
// info, interned vertex-buffer layout, topology). GPU buffers are owned by the
// MeshAllocator (see allocate_and_free_meshes); RenderAsset preparation here
// only builds the metadata.
template <>
struct epix::render::RenderAsset<epix::mesh::Mesh> {
    using ProcessedAsset = epix::mesh::RenderMesh;
    using ExtractedAsset = epix::mesh::Mesh;
    using Param          = epix::ecs::ParamSet<epix::ecs::ResMut<epix::mesh::MeshVertexBufferLayouts>>;

    std::expected<ProcessedAsset, epix::render::PrepareAssetError<epix::mesh::Mesh>> prepare_asset(
        epix::mesh::Mesh&& mesh, epix::assets::AssetId<epix::mesh::Mesh>, Param params, const ProcessedAsset*) {
        auto&& [layouts] = params.get();

        epix::mesh::RenderMeshBufferInfo buffer_info;
        if (auto indices = mesh.get_indices(); indices) {
            const auto& index = indices->get();
            buffer_info = epix::mesh::RenderMeshBufferInfo::indexed(
                static_cast<std::uint32_t>(index.size()),
                index.is_u16() ? wgpu::IndexFormat::eUint16 : wgpu::IndexFormat::eUint32);
        } else {
            buffer_info = epix::mesh::RenderMeshBufferInfo::non_indexed();
        }
        return epix::mesh::RenderMesh::from_metadata(
            static_cast<std::uint32_t>(mesh.count_vertices()), std::move(buffer_info),
            mesh.get_mesh_vertex_buffer_layout(*layouts), mesh.get_primitive_type());
    }

    epix::render::RenderAssetUsages usage(const epix::mesh::Mesh& mesh) noexcept {
        (void)mesh;
        return epix::render::RenderAssetUsages::RENDER_WORLD;
    }

    /** @brief Estimated GPU payload in bytes (Bevy `RenderAsset::byte_len` for
     * `RenderMesh`). Sums the per-vertex attribute stride over the vertex count,
     * plus the index bytes. Used by the render-asset byte limiter. */
    std::optional<std::size_t> byte_len(const epix::mesh::Mesh& mesh) const {
        std::size_t vertex_size = 0;
        for (const auto& data : mesh.iter_attributes()) {
            vertex_size += epix::mesh::vertex_format_size(data.attribute.format);
        }
        const std::size_t vertex_count = mesh.count_vertices();
        std::size_t index_bytes        = 0;
        if (auto indices = mesh.get_indices(); indices) {
            const auto& index = indices->get();
            index_bytes       = index.size() * (index.is_u16() ? sizeof(std::uint16_t) : sizeof(std::uint32_t));
        }
        return vertex_size * vertex_count + index_bytes;
    }

    std::expected<epix::mesh::Mesh, epix::render::AssetExtractionError> take_gpu_data(
        epix::mesh::Mesh& source, const ProcessedAsset*) const {
        if (auto extracted = source.take_gpu_data()) {
            return std::move(*extracted);
        }
        return std::unexpected(epix::render::AssetExtractionError::AlreadyExtracted);
    }
};

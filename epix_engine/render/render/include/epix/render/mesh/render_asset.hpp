#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/fallback_image.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <epix/render/mesh/render_mesh.hpp>
#endif

namespace epix::render::mesh {
/** @brief Per-vertex byte stride of a mesh's packed vertex data (Bevy
 * `MeshVertexBufferLayout::array_stride`): the sum of all attribute sizes. */
EPIX_EXPORT std::uint32_t vertex_array_stride(const epix::mesh::Mesh& mesh);
/** @brief Pack a mesh's per-attribute arrays into Bevy's interleaved per-vertex
 * vertex buffer (`Mesh::write_packed_vertex_buffer_data`): each vertex holds its
 * attributes in slot order. */
EPIX_EXPORT std::vector<std::uint8_t> packed_vertex_bytes(const epix::mesh::Mesh& mesh);

/** @brief Installs `RenderAsset<Mesh>`, the mesh allocator, and the render-world
 * vertex-layout interner (Bevy `MeshRenderAssetPlugin`). */
EPIX_EXPORT struct MeshRenderAssetPlugin {
    void attach(epix::app::App& app);
};
}  // namespace epix::render::mesh

// Bevy 0.18 `RenderAsset for RenderMesh` (mesh/mod.rs:124-202): the render
// world representation of a Mesh is lightweight metadata (vertex count, index
// info, interned vertex-buffer layout, topology). GPU buffers are owned by the
// MeshAllocator (see allocate_and_free_meshes); RenderAsset preparation here
// only builds the metadata.
template <>
struct epix::render::RenderAsset<epix::mesh::Mesh> {
    using ProcessedAsset = epix::render::mesh::RenderMesh;
    using ExtractedAsset = epix::mesh::Mesh;
    using Param = epix::ecs::ParamSet<epix::ecs::Res<epix::render::RenderAssets<epix::image::Image>>,
                                      epix::ecs::ResMut<epix::mesh::MeshVertexBufferLayouts>>;

    std::expected<ProcessedAsset, epix::render::PrepareAssetError<epix::mesh::Mesh>> prepare_asset(
        epix::mesh::Mesh&& mesh, epix::assets::AssetId<epix::mesh::Mesh>, Param params, const ProcessedAsset*) {
        auto&& [images, layouts] = params.get();
        (void)images;

        epix::render::mesh::RenderMeshBufferInfo buffer_info;
        if (auto indices = mesh.indices(); indices) {
            const auto& index = indices->get();
            buffer_info = epix::render::mesh::RenderMeshBufferInfo::indexed(static_cast<std::uint32_t>(index.len()),
                                                                            static_cast<wgpu::IndexFormat>(index));
        } else {
            buffer_info = epix::render::mesh::RenderMeshBufferInfo::non_indexed();
        }
        return epix::render::mesh::RenderMesh{
            .vertex_count = static_cast<std::uint32_t>(mesh.count_vertices()),
            .buffer_info = std::move(buffer_info),
            .key_bits = epix::mesh::BaseMeshPipelineKey::from_primitive_topology(mesh.get_primitive_type()),
            .layout = mesh.get_mesh_vertex_buffer_layout(*layouts),
        };
    }

    epix::assets::RenderAssetUsages usage(const epix::mesh::Mesh& mesh) noexcept { return mesh.asset_usage; }

    /** @brief Estimated GPU payload in bytes (Bevy `RenderAsset::byte_len` for
     * `RenderMesh`). Sums the per-vertex attribute stride over the vertex count,
     * plus the index bytes. Used by the render-asset byte limiter. */
    std::optional<std::size_t> byte_len(const epix::mesh::Mesh& mesh) const {
        std::size_t vertex_size = 0;
        for (const auto& [attribute, values] : mesh.attributes()) {
            vertex_size += epix::mesh::vertex_format_size(attribute.format);
        }
        const std::size_t vertex_count = mesh.count_vertices();
        std::size_t index_bytes        = 0;
        if (const auto bytes = mesh.get_index_buffer_bytes()) index_bytes = bytes->size();
        return vertex_size * vertex_count + index_bytes;
    }

    std::expected<epix::mesh::Mesh, epix::render::AssetExtractionError> take_gpu_data(epix::mesh::Mesh& source,
                                                                                      const ProcessedAsset*) const {
        if (auto extracted = source.take_gpu_data()) {
            return std::move(*extracted);
        }
        return std::unexpected(epix::render::AssetExtractionError::AlreadyExtracted);
    }
};

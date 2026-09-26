#pragma once


#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <epix/common.hpp>
#endif
#include <epix/render/assets.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/mesh/render_mesh.hpp>

namespace epix::render::mesh {
/** @brief Installs `RenderAsset<Mesh>`, the mesh allocator, and the render-world
 * vertex-layout interner (Bevy `MeshRenderAssetPlugin`). */
EPIX_EXPORT struct EPIX_API MeshRenderAssetPlugin {
    void attach(epix::app::App& app);
};

/** @brief Inherit canonical glTF node weights onto direct mesh-primitive children. */
EPIX_EXPORT EPIX_API void inherit_weights(
    epix::ecs::Query<epix::ecs::Item<const epix::ecs::Children&, const epix::mesh::MorphWeights&>,
                     epix::ecs::Filter<epix::ecs::Without<epix::mesh::Mesh3d>,
                                       epix::ecs::Modified<epix::mesh::MorphWeights>>> morph_nodes,
    epix::ecs::Query<epix::ecs::Item<epix::ecs::Mut<epix::mesh::MeshMorphWeights>>, epix::ecs::With<epix::mesh::Mesh3d>>
        morph_primitives);

/** @brief Installs Bevy's PostUpdate direct-child morph-weight inheritance. */
EPIX_EXPORT struct EPIX_API MorphPlugin {
    void attach(epix::app::App& app);
};
}  // namespace epix::render::mesh

// Bevy 0.18 `RenderAsset for RenderMesh` (mesh/mod.rs:124-202): the render
// world representation of a Mesh is lightweight metadata (vertex count, index
// info, interned vertex-buffer layout, topology). GPU buffers are owned by the
// MeshAllocator (see allocate_and_free_meshes); RenderAsset preparation here
// only builds the metadata.
template <>
struct epix::render::RenderAsset<epix::render::mesh::RenderMesh> {
    using SourceAsset    = epix::mesh::Mesh;
    using ExtractedAsset = epix::mesh::Mesh;
    using Param = std::tuple<epix::ecs::Res<epix::render::RenderAssets<epix::render::texture::GpuImage>>,
                             epix::ecs::ResMut<epix::mesh::MeshVertexBufferLayouts>>;

    std::expected<epix::render::mesh::RenderMesh, epix::render::PrepareAssetError<epix::mesh::Mesh>> prepare_asset(
        epix::mesh::Mesh&& mesh, epix::assets::AssetId<epix::mesh::Mesh>, Param params,
        const epix::render::mesh::RenderMesh*) {
        // Bevy passes `SystemParamItem<Self::Param>` already resolved.
        auto&& [images, layouts] = params;

        std::optional<wgpu::TextureView> morph_targets;
        if (auto target_handle = mesh.morph_targets()) {
            const auto* target_image = images->get(target_handle->get().id());
            if (target_image == nullptr) {
                return std::unexpected(epix::render::PrepareAssetError<epix::mesh::Mesh>{
                    epix::render::RetryNextAssetUpdate<epix::mesh::Mesh>{std::move(mesh)}});
            }
            morph_targets = target_image->texture_view;
        }

        epix::render::mesh::RenderMeshBufferInfo buffer_info;
        if (auto indices = mesh.indices(); indices) {
            const auto& index = indices->get();
            buffer_info = epix::render::mesh::RenderMeshBufferInfo::indexed(static_cast<std::uint32_t>(index.len()),
                                                                            static_cast<wgpu::IndexFormat>(index));
        } else {
            buffer_info = epix::render::mesh::RenderMeshBufferInfo::non_indexed();
        }
        auto key_bits = epix::mesh::BaseMeshPipelineKey::from_primitive_topology(mesh.primitive_topology());
        if (morph_targets) key_bits |= epix::mesh::BaseMeshPipelineKey::MORPH_TARGETS;

        return epix::render::mesh::RenderMesh{
            .vertex_count  = static_cast<std::uint32_t>(mesh.count_vertices()),
            .morph_targets = std::move(morph_targets),
            .buffer_info   = std::move(buffer_info),
            .key_bits      = key_bits,
            .layout        = mesh.get_mesh_vertex_buffer_layout(*layouts),
        };
    }

    epix::assets::RenderAssetUsages asset_usage(const epix::mesh::Mesh& mesh) noexcept { return mesh.asset_usage; }

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

    std::expected<epix::mesh::Mesh, epix::render::AssetExtractionError> take_gpu_data(
        epix::mesh::Mesh& source, const epix::render::mesh::RenderMesh*) const {
        if (auto extracted = source.take_gpu_data()) {
            return std::move(*extracted);
        }
        return std::unexpected(epix::render::AssetExtractionError::AlreadyExtracted);
    }
};

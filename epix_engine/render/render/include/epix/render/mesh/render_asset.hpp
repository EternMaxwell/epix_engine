#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/image.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <functional>
#include <optional>
#include <ranges>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
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
    using Param          = std::tuple<epix::ecs::Res<epix::render::RenderAssets<epix::render::texture::GpuImage>>,
                                      epix::ecs::ResMut<epix::mesh::MeshVertexBufferLayouts>>;

    EPIX_API std::expected<epix::render::mesh::RenderMesh, epix::render::PrepareAssetError<epix::mesh::Mesh>>
    prepare_asset(epix::mesh::Mesh&& mesh,
                  epix::assets::AssetId<epix::mesh::Mesh> id,
                  Param params,
                  const epix::render::mesh::RenderMesh* previous);

    epix::assets::RenderAssetUsages asset_usage(const epix::mesh::Mesh& mesh) noexcept { return mesh.asset_usage; }

    /** @brief Estimated GPU payload in bytes (Bevy `RenderAsset::byte_len` for
     * `RenderMesh`). Sums the per-vertex attribute stride over the vertex count,
     * plus the index bytes. Used by the render-asset byte limiter. */
    EPIX_API std::optional<std::size_t> byte_len(const epix::mesh::Mesh& mesh) const;

    std::expected<epix::mesh::Mesh, epix::render::AssetExtractionError> take_gpu_data(
        epix::mesh::Mesh& source, const epix::render::mesh::RenderMesh*) const {
        if (auto extracted = source.take_gpu_data()) {
            return std::move(*extracted);
        }
        return std::unexpected(epix::render::AssetExtractionError::AlreadyExtracted);
    }
};

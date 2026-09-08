#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/mesh/base_mesh_pipeline.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <utility>
#include <variant>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::mesh {

/** @brief Indexed-buffer metadata of a render mesh (Bevy
 * `RenderMeshBufferInfo::Indexed`): the index count and format. */
EPIX_EXPORT struct RenderMeshBufferInfoIndexed {
    std::uint32_t count = 0;
    wgpu::IndexFormat index_format = wgpu::IndexFormat::eUint16;

    bool operator==(const RenderMeshBufferInfoIndexed&) const noexcept = default;
};

/** @brief Index/vertex buffer metadata of a render mesh (Bevy
 * `RenderMeshBufferInfo`): either `Indexed { count, index_format }` or
 * `NonIndexed`. The actual GPU buffers come from the `MeshAllocator`; this
 * only describes how to draw them. */
EPIX_EXPORT struct RenderMeshBufferInfo {
    /** @brief `std::monostate` represents Bevy's `NonIndexed` variant. */
    std::variant<RenderMeshBufferInfoIndexed, std::monostate> value{std::monostate{}};

    static RenderMeshBufferInfo indexed(std::uint32_t count, wgpu::IndexFormat format) {
        return RenderMeshBufferInfo{RenderMeshBufferInfoIndexed{.count = count, .index_format = format}};
    }
    static RenderMeshBufferInfo non_indexed() { return RenderMeshBufferInfo{std::monostate{}}; }

    bool is_indexed() const noexcept { return std::holds_alternative<RenderMeshBufferInfoIndexed>(value); }
    const RenderMeshBufferInfoIndexed* indexed_info() const noexcept { return std::get_if<RenderMeshBufferInfoIndexed>(&value); }
};

/** @brief The render world representation of a `Mesh` (Bevy 0.18 `RenderMesh`):
 * lightweight CPU metadata only. GPU buffers live in the `MeshAllocator`
 * (`mesh_vertex_slice`/`mesh_index_slice`); this type carries the vertex
 * buffer layout reference and the draw metadata. */
EPIX_EXPORT struct RenderMesh {
    /** @brief The number of vertices in the mesh. */
    std::uint32_t vertex_count = 0;
    /** @brief Information about the mesh data buffers, including whether the
     * mesh uses indices or not (Bevy `buffer_info`). */
    RenderMeshBufferInfo buffer_info;
    /** @brief Precomputed base pipeline bits (Bevy `key_bits`). */
    epix::mesh::BaseMeshPipelineKey key_bits;
    /** @brief A reference to the interned vertex buffer layout (Bevy `layout`). */
    epix::mesh::MeshVertexBufferLayoutRef layout;

    /** @brief The primitive topology of this mesh. */
    wgpu::PrimitiveTopology primitive_topology() const noexcept { return key_bits.primitive_topology(); }
    /** @brief True when this mesh uses an index buffer (Bevy `indexed()`). */
    bool indexed() const noexcept { return buffer_info.is_indexed(); }
};

}  // namespace epix::render::mesh

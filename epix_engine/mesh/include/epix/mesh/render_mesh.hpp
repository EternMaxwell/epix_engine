#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <variant>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/mesh/vertex_buffer_layout.hpp>

namespace epix::mesh {

/** @brief Indexed-buffer metadata of a render mesh (Bevy
 * `RenderMeshBufferInfo::Indexed`): the index count and format. */
struct RenderMeshBufferInfoIndexed {
    std::uint32_t count = 0;
    wgpu::IndexFormat index_format = wgpu::IndexFormat::eUint16;

    bool operator==(const RenderMeshBufferInfoIndexed&) const noexcept = default;
};

/** @brief Index/vertex buffer metadata of a render mesh (Bevy
 * `RenderMeshBufferInfo`): either `Indexed { count, index_format }` or
 * `NonIndexed`. The actual GPU buffers come from the `MeshAllocator`; this
 * only describes how to draw them. */
struct RenderMeshBufferInfo {
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
struct RenderMesh {
    /** @brief The number of vertices in the mesh. */
    std::uint32_t vertex_count = 0;
    /** @brief Information about the mesh data buffers, including whether the
     * mesh uses indices or not (Bevy `buffer_info`). */
    RenderMeshBufferInfo buffer_info;
    /** @brief A reference to the interned vertex buffer layout (Bevy `layout`). */
    MeshVertexBufferLayoutRef layout;
    /** @brief Primitive topology of the mesh (Bevy derives this from
     * `BaseMeshPipelineKey`). */
    wgpu::PrimitiveTopology primitive_topology = wgpu::PrimitiveTopology::eTriangleList;

    static RenderMesh from_metadata(std::uint32_t vertex_count, RenderMeshBufferInfo buffer_info,
                                    MeshVertexBufferLayoutRef layout,
                                    wgpu::PrimitiveTopology primitive_topology) {
        return RenderMesh{vertex_count, std::move(buffer_info), std::move(layout), primitive_topology};
    }

    /** @brief The primitive topology of this mesh. */
    wgpu::PrimitiveTopology primitive_type() const noexcept { return primitive_topology; }
    /** @brief True when this mesh uses an index buffer (Bevy `indexed()`). */
    bool indexed() const noexcept { return buffer_info.is_indexed(); }
};

}  // namespace epix::mesh

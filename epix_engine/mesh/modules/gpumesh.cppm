module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <variant>
#include <vector>
#endif
export module epix.mesh:gpumesh;

import :mesh;

import epix.render;
import epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import webgpu;

namespace epix::mesh {
/** @brief GPU-side mesh storing vertex/index buffers uploaded from a Mesh.
 *
 * Created from a CPU Mesh via create_from_mesh, and can be bound to a render pass.
 */
export struct GPUMesh {
   public:
    /** @brief Create a GPUMesh from a CPU Mesh, using default device limits.
     *  @param mesh The source mesh.
     *  @param device The wgpu device for buffer allocation. */
    static GPUMesh create_from_mesh(const Mesh& mesh, const wgpu::Device& device);
    /** @brief Create a GPUMesh from a CPU Mesh with explicit device limits.
     *  @param mesh The source mesh.
     *  @param device The wgpu device for buffer allocation.
     *  @param limits Device limits for alignment constraints. */
    static GPUMesh create_from_mesh(const Mesh& mesh, const wgpu::Device& device, const wgpu::Limits& limits);
    /** @brief Re-upload mesh data from a CPU Mesh to existing GPU buffers.
     *  @param mesh The source mesh.
     *  @param device The wgpu device.
     *  @param limits Device limits for alignment constraints. */
    void update_from_mesh(const Mesh& mesh, const wgpu::Device& device, const wgpu::Limits& limits);
    /** @brief Check whether this mesh uses indexed drawing. */
    bool is_indexed() const { return _index_binding.has_value(); }
    /** @brief Get the number of vertices (or indices if indexed). */
    std::size_t vertex_count() const { return _vertex_count; }
    /** @brief Get the primitive topology of this mesh. */
    wgpu::PrimitiveTopology primitive_type() const { return _primitive_type; }
    /** @brief Bind vertex and index buffers to a render pass encoder. */
    void bind_to(const wgpu::RenderPassEncoder& encoder) const;
    /** @brief Iterate over the mesh attribute descriptors. */
    auto iter_attributes() const { return std::views::values(_attributes); }
    /** @brief Check whether this mesh contains a specific attribute. */
    bool contains_attribute(const MeshAttribute& attribute) const {
        auto it = _attributes.find(attribute.slot);
        return it != _attributes.end() && it->second == attribute;
    }
    /** @brief Get the full attribute layout map. */
    const MeshAttributeLayout& attribute_layout() const { return _attributes; }

   private:
    GPUMesh()
        : _primitive_type(wgpu::PrimitiveTopology::eTriangleList),
          _combined_buffer(nullptr),
          _index_buffer(nullptr),
          _vertex_count(0) {}

    struct VertexBindingInfo {
        std::uint32_t shader_location;
        std::size_t offset;
        std::size_t size;
    };
    struct IndexBindingInfo {
        wgpu::IndexFormat format;
        std::uint32_t offset;
        std::size_t size;
    };

    MeshAttributeLayout _attributes;

    wgpu::PrimitiveTopology _primitive_type;
    wgpu::Buffer _combined_buffer;
    wgpu::Buffer _index_buffer;
    std::vector<VertexBindingInfo> _attribute_bindings;
    std::optional<IndexBindingInfo> _index_binding;
    std::size_t _vertex_count;  // or index count if indexed
};
}  // namespace mesh

template <>
struct epix::render::RenderAsset<epix::mesh::Mesh> {
    using ProcessedAsset = epix::mesh::GPUMesh;
    using Param          = epix::core::ParamSet<epix::core::Res<wgpu::Device>, epix::core::Res<wgpu::Limits>>;

    ProcessedAsset process(const epix::mesh::Mesh& mesh, Param params) {
        auto&& [device, limits] = params.get();
        return ProcessedAsset::create_from_mesh(mesh, *device, *limits);
    }

    epix::render::RenderAssetUsage usage(const epix::mesh::Mesh& mesh) {
        return epix::render::RenderAssetUsageBits::RenderWorld;
    }
};

export namespace std {
template <>
struct hash<epix::assets::AssetId<epix::mesh::Mesh>> {
    std::size_t operator()(const epix::assets::AssetId<epix::mesh::Mesh>& id) const {
        return std::visit([]<typename T>(const T& value) { return std::hash<T>()(value); }, id);
    }
};
}  // namespace std
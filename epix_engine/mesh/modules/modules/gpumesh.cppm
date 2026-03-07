module;

export module epix.mesh:gpumesh;

import :mesh;

import epix.render;
import webgpu;

namespace mesh {
export struct GPUMesh {
   public:
    static GPUMesh create_from_mesh(const Mesh& mesh, const wgpu::Device& device);
    void update_from_mesh(const Mesh& mesh, const wgpu::Device& device);
    bool is_indexed() const { return _index_binding.has_value(); }
    std::size_t vertex_count() const { return _vertex_count; }
    wgpu::PrimitiveTopology primitive_type() const { return _primitive_type; }
    void bind_to(const wgpu::RenderPassEncoder& encoder) const;
    auto iter_attributes() const { return std::views::values(_attributes); }
    bool contains_attribute(const MeshAttribute& attribute) const {
        auto it = _attributes.find(attribute.slot);
        return it != _attributes.end() && it->second == attribute;
    }
    const MeshAttributeLayout& attribute_layout() const { return _attributes; }

   private:
    GPUMesh()
        : _primitive_type(wgpu::PrimitiveTopology::eTriangleList),
          _combined_buffer(nullptr),
          _index_buffer(nullptr),
          _vertex_count(0) {}

    struct VertexBindingInfo {
        std::uint32_t slot;
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
struct render::RenderAsset<mesh::Mesh> {
    using ProcessedAsset = mesh::GPUMesh;
    using Param          = core::Res<wgpu::Device>;

    ProcessedAsset process(const mesh::Mesh& mesh, Param device) {
        return ProcessedAsset::create_from_mesh(mesh, *device);
    }

    render::RenderAssetUsage usage(const mesh::Mesh& mesh) { return render::RenderAssetUsageBits::RenderWorld; }
};
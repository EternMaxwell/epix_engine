#pragma once

#include "mesh.hpp"

namespace epix::mesh {
struct GPUMesh {
   public:
    static GPUMesh create_from_mesh(const Mesh& mesh, nvrhi::DeviceHandle device);
    void update_from_mesh(const Mesh& mesh, nvrhi::DeviceHandle device);
    bool is_indexed() const { return _index_binding.has_value(); }
    size_t vertex_count() const { return _vertex_count; }
    nvrhi::PrimitiveType primitive_type() const { return _primitive_type; }
    void bind_state(nvrhi::GraphicsState& state) const;
    auto iter_attributes() const { return std::views::values(_attributes); }
    bool contains_attribute(const MeshAttribute& attribute) const {
        auto it = _attributes.find(attribute.slot);
        return it != _attributes.end() && it->second == attribute;
    }
    const MeshAttributeLayout& attribute_layout() const { return _attributes; }

   private:
    GPUMesh()
        : _primitive_type(nvrhi::PrimitiveType::PointList),
          _combined_buffer(nullptr),
          _index_buffer(nullptr),
          _vertex_count(0) {}

    struct VertexBindingInfo {
        uint32_t slot;
        size_t offset;
    };
    struct IndexBindingInfo {
        nvrhi::Format format;
        uint32_t offset;
    };

    MeshAttributeLayout _attributes;

    nvrhi::PrimitiveType _primitive_type;
    nvrhi::BufferHandle _combined_buffer;
    nvrhi::BufferHandle _index_buffer;
    std::vector<VertexBindingInfo> _attribute_bindings;
    std::optional<IndexBindingInfo> _index_binding;
    size_t _vertex_count;  // or index count if indexed
};
}  // namespace epix::mesh
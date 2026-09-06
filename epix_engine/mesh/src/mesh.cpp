

#include <epix/assets.hpp>
#include <epix/mesh.hpp>

using namespace epix::mesh;

Mesh::Mesh(const Mesh& other) : asset_usage(other.asset_usage), primitive_type(other.primitive_type) {
    for (const auto& [slot, attribute_data] : other._attributes) {
        _attributes.emplace(slot, MeshAttributeData{
                                      .attribute = attribute_data.attribute,
                                      .data      = attribute_data.data.clone(),
                                  });
    }
    if (other._indices) {
        _indices.emplace(other._indices->data.clone());
    }
}

Mesh& Mesh::operator=(const Mesh& other) {
    if (this == std::addressof(other)) return *this;
    Mesh copy(other);
    *this = std::move(copy);
    return *this;
}

MeshAttributeLayout Mesh::attribute_layout() const {
    MeshAttributeLayout layout;
    layout.primitive_type = primitive_type;
    for (const auto& [slot, attribute_data] : _attributes) {
        layout.insert_or_assign(slot, attribute_data.attribute);
    }
    return layout;
}

MeshVertexBufferLayoutRef Mesh::get_mesh_vertex_buffer_layout(MeshVertexBufferLayouts& mesh_vertex_buffer_layouts) const {
    // Bevy Mesh::get_mesh_vertex_buffer_layout: the layout's attributes are
    // built in attribute-id (Bevy) / slot (Epix) order with accumulated byte
    // offsets and a stride equal to the total size.
    std::vector<MeshVertexAttributeId> attribute_ids;
    std::vector<VertexAttributeDescriptor> attributes;
    std::uint64_t accumulated_offset = 0;
    for (const auto& [slot, attribute_data] : _attributes) {
        attribute_ids.push_back(MeshVertexAttributeId{static_cast<std::uint64_t>(slot)});
        attributes.push_back(VertexAttributeDescriptor{
            .offset          = accumulated_offset,
            .format          = attribute_data.attribute.format,
            .shader_location = static_cast<std::uint32_t>(slot),
        });
        accumulated_offset += vertex_format_size(attribute_data.attribute.format);
    }
    return mesh_vertex_buffer_layouts.insert(MeshVertexBufferLayout{
        .attribute_ids = std::move(attribute_ids),
        .layout        = VertexBufferLayout{.array_stride = accumulated_offset,
                                            .step_mode    = wgpu::VertexStepMode::eVertex,
                                            .attributes   = std::move(attributes)},
    });
}

std::optional<epix::camera::Aabb> Mesh::compute_aabb() const {
    const auto attribute = get_attribute(ATTRIBUTE_POSITION);
    if (!attribute || attribute->get().data.type_info() != epix::meta::type_info::of<glm::vec3>()) return std::nullopt;
    const auto positions = attribute->get().data.cspan_as<glm::vec3>();
    if (positions.empty()) return std::nullopt;
    glm::vec3 minimum = positions.front();
    glm::vec3 maximum = positions.front();
    for (const glm::vec3& position : positions) {
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    return epix::camera::Aabb::from_min_max(minimum, maximum);
}

std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> Mesh::get_attribute(
    const MeshAttribute& attribute) const {
    auto it = _attributes.find(attribute.slot);
    if (it == _attributes.end()) {
        return std::unexpected(MeshError::SlotNotFound);
    } else if (it->second.attribute.name != attribute.name) {
        return std::unexpected(MeshError::NameMismatch);
    } else if (it->second.attribute.format != attribute.format) {
        return std::unexpected(MeshError::TypeMismatch);
    }
    return std::cref(it->second);
}

std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> Mesh::get_attribute(std::size_t slot) const {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        return std::cref(it->second);
    }
    return std::unexpected(MeshError::SlotNotFound);
}

std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> Mesh::get_attribute_mut(
    const MeshAttribute& attribute) {
    auto it = _attributes.find(attribute.slot);
    if (it == _attributes.end()) {
        return std::unexpected(MeshError::SlotNotFound);
    } else if (it->second.attribute.name != attribute.name) {
        return std::unexpected(MeshError::NameMismatch);
    } else if (it->second.attribute.format != attribute.format) {
        return std::unexpected(MeshError::TypeMismatch);
    }
    return std::ref(it->second);
}

std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> Mesh::get_attribute_mut(std::size_t slot) {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        return std::ref(it->second);
    }
    return std::unexpected(MeshError::SlotNotFound);
}

std::optional<Mesh> Mesh::take_gpu_data() {
    if (_attributes.empty() && (!_indices.has_value() || _indices->empty())) return std::nullopt;

    Mesh extracted{primitive_type, asset_usage};
    extracted._attributes = std::move(_attributes);
    extracted._indices    = std::move(_indices);
    return extracted;
}

std::expected<MeshAttributeData, MeshError> Mesh::remove_attribute(const MeshAttribute& attribute) {
    auto it = _attributes.find(attribute.slot);
    if (it == _attributes.end()) {
        return std::unexpected(MeshError::SlotNotFound);
    } else if (it->second.attribute.name != attribute.name) {
        return std::unexpected(MeshError::NameMismatch);
    } else if (it->second.attribute.format != attribute.format) {
        return std::unexpected(MeshError::TypeMismatch);
    }
    MeshAttributeData data = std::move(it->second);
    _attributes.erase(it);
    return data;
}
std::expected<MeshAttributeData, MeshError> Mesh::remove_attribute(std::size_t slot) {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        MeshAttributeData data = std::move(it->second);
        _attributes.erase(it);
        return data;
    }
    return std::unexpected(MeshError::SlotNotFound);
}

void MeshPlugin::attach(app::App& app) { assets::app_register_asset<Mesh>(app); }
